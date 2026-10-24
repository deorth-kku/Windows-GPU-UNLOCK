/* Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md. */ #include "app_io.h"
#include "compat/compat.h"
#include "format.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,InfoGuid=EFI_FILE_INFO_ID;
static EFI_SYSTEM_TABLE *Table;
static EFI_FILE_PROTOCOL *Root,*Parent,*Run,*Report;
static BOOLEAN LogFault;
/* Log lines are batched in RAM: a per-line Write+Flush on the FAT volume
   costs a directory/FAT commit on every line. Flush at half full, on close,
   and on Halt so every failure path still reaches the file. */
static char LogBuffer[4096];
static UINTN LogBufferLen=0;
static VOID LogError(VOID);
/* Caller must guarantee Report is valid and !LogFault. */
static VOID LogFlush(VOID) {
  if(!LogBufferLen)return;
  UINTN written=LogBufferLen;
  EFI_STATUS status=Report->Write(Report,&written,LogBuffer);
  if(EFI_ERROR(status) || written!=LogBufferLen){LogError();return;}
  LogBufferLen=0;
  if(EFI_ERROR(Report->Flush(Report)))LogError();
}
#ifdef GU_HOST_WRITE_TEST
/* Host unit-test hook: point GuLoadEspProfile at a mocked directory handle. */
VOID GuIoTestSetParent(EFI_FILE_PROTOCOL *P){Parent=P;}
#endif
typedef struct {const char *name;CHAR16 *file;UINTN size;const char *hash;} ASSET_SPEC;
static const ASSET_SPEC Specs[]={
 {"gpu-unlock/610.43.03/booter_load_ga102.bin",L"ASSETS\\booter_load_ga102.bin",61256,"056cbc064a888f80717726c54d7f64fcd68044d123870c95a74f37f4975dcaa4"},
 {"gpu-unlock/610.43.03/v67-ga102-610.43.03.bin",L"ASSETS\\v67-ga102-610.43.03.bin",64000,"2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197"},
 {"gpu-unlock/610.43.03/gsp_ga10x.bin",L"ASSETS\\gsp_ga10x.bin",84277400,"572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a"},
 {"gpu-unlock/610.43.03/gsp_boot_ga102.bin",L"ASSETS\\gsp_boot_ga102.bin",24576,"3df651acb174fd5f5561a1996965b192f50c5d763168446ed0d4ead19d895768"},
 {"gpu-unlock/610.43.03/gsp_boot_desc_ga102.bin",L"ASSETS\\gsp_boot_desc_ga102.bin",84,"cc5a3fc1f4c47f2fe107b1b68f0176d8a448e6dec84b575e5560ccef5262bf15"}
};
static VOID LogError(VOID){LogFault=TRUE;if(Gu.armed)Gu.fault=TRUE;}
VOID GuConsole(const char *Text) {
  if(!Table || !Table->ConOut || !Table->ConOut->OutputString){LogError();return;}
  while(*Text) {
    CHAR16 line[128];UINTN n=0;
    while(*Text && n<126){if(*Text=='\n')line[n++]='\r';line[n++]=(UINT8)*Text++;}
    line[n]=0;if(EFI_ERROR(Table->ConOut->OutputString(Table->ConOut,line))){LogError();return;}
  }
}
VOID GuLogText(const char *Text) {
  GuConsole(Text);
  if(!Report || LogFault){LogError();return;}
  UINTN expected=strlen(Text);
  if(expected>=sizeof(LogBuffer)) {
    /* Longer than the batch buffer (not expected: GuLog caps at 1024):
       write directly, keeping the old per-write durability for this line.
       Flush pending lines first: Report has no APPEND mode, so a direct
       write lands at the current file position and would otherwise be
       followed by the earlier buffered lines (order inversion). */
    LogFlush();
    if(LogFault)return;
    UINTN written=expected;
    EFI_STATUS status=Report->Write(Report,&written,(VOID *)Text);
    if(EFI_ERROR(status) || written!=expected || EFI_ERROR(Report->Flush(Report)))LogError();
    return;
  }
  if(LogBufferLen+expected>sizeof(LogBuffer))LogFlush();
  if(LogFault)return;
  memcpy(LogBuffer+LogBufferLen,Text,expected);
  LogBufferLen+=expected;
  if(LogBufferLen>=sizeof(LogBuffer)/2)LogFlush();
}
VOID GuLog(const char *Format,...) {
  char text[1024];va_list args;va_start(args,Format);
  size_t n=GuFormatV(text,sizeof(text),Format,args);va_end(args);
  if(n>=sizeof(text)){LogError();GuConsole("Log format or length error.\n");return;}
  GuLogText(text);
}
VOID GuLogFlush(VOID) {
  if(Report && !LogFault)LogFlush();
}
BOOLEAN GuLogReady(VOID){return Report && !LogFault;}

static EFI_STATUS Metadata(EFI_FILE_PROTOCOL *File,BOOLEAN Directory,UINTN Expected) {
  union {EFI_FILE_INFO info;UINT8 bytes[512];} data;UINTN n=sizeof(data);
  EFI_STATUS status=File->GetInfo(File,&InfoGuid,&n,&data);
  if(EFI_ERROR(status))return status;
  if(n<SIZE_OF_EFI_FILE_INFO || n>sizeof(data) || data.info.Size>n || data.info.Size<SIZE_OF_EFI_FILE_INFO ||
    !!(data.info.Attribute&EFI_FILE_DIRECTORY)!=!!Directory ||
    (!Directory && data.info.FileSize!=Expected))return EFI_COMPROMISED_DATA;
  return EFI_SUCCESS;
}
/* Parse machine_profile.txt (key=value lines) into the runtime ESP target.
   The booted ESP is the only accepted source; nothing is baked into the image. */
static UINTN GuHexVal(char c) {
  if(c>='0'&&c<='9')return (UINTN)(c-'0');
  if(c>='a'&&c<='f')return (UINTN)(c-'a')+10;
  if(c>='A'&&c<='F')return (UINTN)(c-'A')+10;
  return 255;
}
static BOOLEAN GuParseHex(const char *s,UINTN n,UINT64 *out) {
  UINT64 v=0;UINTN i;
  if(!s)return FALSE;
  for(i=0;i<n;i++){UINTN d=GuHexVal(s[i]);if(d>15)return FALSE;v=(v<<4)|d;}
  *out=v;return TRUE;
}
static BOOLEAN GuParseDec(const char *s,UINT64 *out) {
  UINT64 v=0;
  if(!s || !*s)return FALSE;
  while(*s){if(*s<'0'||*s>'9')return FALSE;v=v*10+(UINT64)(*s++-'0');}
  *out=v;return TRUE;
}
static EFI_STATUS GuParseEspProfile(const char *text,UINTN len) {
  char buf[1024];UINTN i,k;
  UINT64 partition=0,start_lba=0,size_lba=0;
  UINT32 countdown=5;
  BOOLEAN have_part=FALSE,have_start=FALSE,have_size=FALSE,have_guid=FALSE;
  EFI_GUID guid={0};
  if(len>=sizeof(buf))return EFI_COMPROMISED_DATA;
  memcpy(buf,text,len);buf[len]=0;
  for(i=0;;) {
    UINTN start=i,j=i;
    BOOLEAN last=FALSE;
    while(buf[j] && buf[j]!='\n')j++;
    if(!buf[j])last=TRUE;
    while(j>start && (buf[j-1]=='\r'||buf[j-1]==' '||buf[j-1]=='\t'))j--;
    /* Terminate the line: values are read via strlen/GuParseDec and must not
       run past the newline into the next line. */
    buf[j]=0;
    if(j>start) {
      char *eq=0;
      for(k=start;k<j;k++)if(buf[k]=='=' ){eq=buf+k;break;}
      if(eq) {
        *eq=0;
        char *key=buf+start,*val=eq+1;
        while(*key==' '||*key=='\t')key++;
        {UINTN kl=(UINTN)strlen(key);while(kl>0 && (key[kl-1]==' '||key[kl-1]=='\t'))key[--kl]=0;}
        while(*val==' '||*val=='\t')val++;
        {UINTN vl=(UINTN)strlen(val);while(vl>0 && (val[vl-1]==' '||val[vl-1]=='\t'))val[--vl]=0;}
        if(!strcmp(key,"windows_esp_guid")) {
          char d[33];UINTN di=0,total=0;UINT64 t1,t2,t3,t4;
          for(k=0;val[k];k++){if(val[k]!='-'){total++;if(di<32)d[di++]=val[k];}}
          d[di]=0;
          /* Strict: exactly 32 valid hex digits, else the GUID is rejected
             (partial writes would leave guid fields uninitialized). */
          if(total==32 && GuParseHex(d,8,&t1) && GuParseHex(d+8,4,&t2) &&
             GuParseHex(d+12,4,&t3) && GuParseHex(d+16,16,&t4)) {
            guid.Data1=(UINT32)t1;guid.Data2=(UINT16)t2;guid.Data3=(UINT16)t3;
            for(k=0;k<8;k++)guid.Data4[k]=(UINT8)((t4>>(56-8*k))&0xff);
            have_guid=TRUE;
          }
        } else if(!strcmp(key,"windows_esp_partition")) {
          if(GuParseDec(val,&partition))have_part=TRUE;
        } else if(!strcmp(key,"windows_esp_start_lba")) {
          if(GuParseDec(val,&start_lba))have_start=TRUE;
        } else if(!strcmp(key,"windows_esp_size_lba")) {
          if(GuParseDec(val,&size_lba))have_size=TRUE;
        } else if(!strcmp(key,"countdown_seconds")) {
          UINT64 cd=0;
          /* 0 means "no countdown, arm immediately"; 1..3600 are seconds. */
          if(GuParseDec(val,&cd) && cd<=3600)countdown=(UINT32)cd;
        }
      }
    }
    if(last)break;
    i=j+1;
  }
  if(!(have_guid&&have_part&&have_start&&have_size))return EFI_COMPROMISED_DATA;
  Gu.esp_guid=guid;
  Gu.esp_partition=partition;
  Gu.esp_start_lba=start_lba;
  Gu.esp_size_lba=size_lba;
  Gu.countdown_seconds=countdown;
  Gu.esp_profile_loaded=TRUE;
  GuLog("esp-profile: partition=%llu start_lba=%llu size_lba=%llu countdown=%u\n",
    (unsigned long long)partition,(unsigned long long)start_lba,(unsigned long long)size_lba,(unsigned)countdown);
  return EFI_SUCCESS;
}
/* Read and parse machine_profile.txt from the payload directory. Must run
   after GuIoOpen so that REPORT.TXT exists and every failure path is logged.
   The booted ESP is the only accepted source; nothing is baked into the image. */
EFI_STATUS GuLoadEspProfile(VOID) {
  EFI_FILE_PROTOCOL *file=NULL;
  union {EFI_FILE_INFO info;UINT8 bytes[512];} data;
  UINTN n=sizeof(data),bytes=0;
  /* No leading backslash: edk2's FAT driver treats a name starting with '\'
     as absolute from the volume root, ignoring the Parent handle. */
  EFI_STATUS status=Parent->Open(Parent,&file,L"machine_profile.txt",EFI_FILE_MODE_READ,0);
  if(EFI_ERROR(status)) {
    GuLog("esp-profile: open machine_profile.txt failed status=%#llx\n",(unsigned long long)status);
    return status;
  }
  n=sizeof(data);
  status=file->GetInfo(file,&InfoGuid,&n,&data);
  if(EFI_ERROR(status) || n<SIZE_OF_EFI_FILE_INFO || n>sizeof(data) ||
     data.info.Size>n || data.info.Size<SIZE_OF_EFI_FILE_INFO ||
     (data.info.Attribute&EFI_FILE_DIRECTORY) || data.info.FileSize<1 || data.info.FileSize>1024) {
    file->Close(file);
    GuLog("esp-profile: file metadata rejected status=%#llx\n",(unsigned long long)status);
    return EFI_COMPROMISED_DATA;
  }
  {
    char *text=GuAlloc(data.info.FileSize+1);
    if(!text){file->Close(file);return EFI_OUT_OF_RESOURCES;}
    /* Read exactly FileSize bytes; a Read with *Len==0 and a non-NULL buffer
       is a zero-byte read, not a size query (that requires Buffer==NULL). */
    bytes=data.info.FileSize;
    status=file->Read(file,&bytes,text);
    if(EFI_ERROR(status) || bytes!=data.info.FileSize) {
      if(!EFI_ERROR(status))(void)file->Close(file);
      GuFree(text);
      GuLog("esp-profile: read failed status=%#llx bytes=%llu\n",(unsigned long long)status,(unsigned long long)bytes);
      return EFI_DEVICE_ERROR;
    }
    text[bytes]=0;
    status=file->Close(file);
    if(!EFI_ERROR(status))status=GuParseEspProfile(text,bytes);
    if(EFI_ERROR(status)) {
      (void)file->Close(file);
      GuLog("esp-profile: parse failed status=%#llx\n",(unsigned long long)status);
    }
    GuFree(text);
  }
  return status;
}
EFI_STATUS GuIoOpen(EFI_HANDLE Image,EFI_SYSTEM_TABLE *St) {
  Table=St;Root=Parent=Run=Report=NULL;LogFault=FALSE;memset(GuAssets,0,sizeof(GuAssets));
  EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
  EFI_STATUS status=Table->BootServices->HandleProtocol(Image,&LoadedGuid,(VOID **)&loaded);
  /* No USB requirement: the payload folder is opened from the booted volume
     root below, so it is guaranteed to live on the booted EFI partition. */
  if(EFI_ERROR(status) || !loaded)return EFI_UNSUPPORTED;
  status=Table->BootServices->HandleProtocol(loaded->DeviceHandle,&FsGuid,(VOID **)&fs);
  if(EFI_ERROR(status) || !fs)return EFI_UNSUPPORTED;
  status=fs->OpenVolume(fs,&Root);if(EFI_ERROR(status) || !Root)return EFI_DEVICE_ERROR;
#if defined(GU_EXPERIMENTAL_3090)
  status=Root->Open(Root,&Parent,L"\\RTX3090-UNLOCK",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
#else
  status=Root->Open(Root,&Parent,L"\\RTX3090-PREFLIGHT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
#endif
  if(EFI_ERROR(status) || !Parent)return EFI_NOT_FOUND;
  status=Metadata(Parent,TRUE,0);if(EFI_ERROR(status))return status;
  /* The ESP profile (machine_profile.txt) is read later, as late as possible
     (see GuLoadEspProfile), so its failure cannot prevent RUN/REPORT creation. */
  for(UINTN slot=0;slot<100;slot++) {
    CHAR16 name[]=L"RUN00";name[3]=(CHAR16)('0'+slot/10);name[4]=(CHAR16)('0'+slot%10);
    EFI_FILE_PROTOCOL *existing=NULL;
    status=Parent->Open(Parent,&existing,name,EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)) {
      if(!existing || EFI_ERROR(existing->Close(existing)))return EFI_DEVICE_ERROR;continue;
    }
    if(status!=EFI_NOT_FOUND)return status;
    status=Parent->Open(Parent,&Run,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(status) || !Run)return EFI_DEVICE_ERROR;
    status=Metadata(Run,TRUE,0);if(EFI_ERROR(status))return status;
    status=Run->Open(Run,&existing,L"REPORT.TXT",EFI_FILE_MODE_READ,0);
    if(!EFI_ERROR(status)){if(existing)existing->Close(existing);return EFI_ALREADY_STARTED;}
    if(status!=EFI_NOT_FOUND)return status;
    status=Run->Open(Run,&Report,L"REPORT.TXT",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(EFI_ERROR(status) || !Report)return EFI_DEVICE_ERROR;
#if defined(GU_EXPERIMENTAL_3090)
    GuLog("RTX3090 AMD EXPERIMENTAL MANUAL UNLOCK\nRUN%02u\nManual U starts one hardware transaction; Esc or timeout cancels. Hardware success unverified.\n",(unsigned)slot);
#else
    GuLog("RTX3090 AMD read-only cold-state preflight\nRUN%02u\nCannot arm: GPU writes, DMA, reset and driver release are blocked.\n",(unsigned)slot);
#endif
    return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
  }
  return EFI_VOLUME_FULL;
}
EFI_STATUS GuLoadAssets(VOID) {
  for(UINTN i=0;i<5;i++) {
    EFI_FILE_PROTOCOL *file=NULL;const ASSET_SPEC *spec=&Specs[i];
    EFI_STATUS status=Parent->Open(Parent,&file,spec->file,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(status) || !file)return EFI_NOT_FOUND;
    status=Metadata(file,FALSE,spec->size);
    UINT8 *data=NULL;
    if(!EFI_ERROR(status)) {
      data=GuAlloc(spec->size);if(!data)status=EFI_OUT_OF_RESOURCES;
    }
    for(UINTN done=0;!EFI_ERROR(status) && done<spec->size;) {
      UINTN n=spec->size-done;if(n>1048576)n=1048576;UINTN want=n;
      status=file->Read(file,&n,data+done);
      if(!EFI_ERROR(status) && n!=want)status=EFI_END_OF_FILE;done+=n;
    }
    EFI_STATUS closed=file->Close(file);if(!EFI_ERROR(status))status=closed;
    if(!EFI_ERROR(status) && !GuHashMatches(data,spec->size,spec->hash))status=EFI_CRC_ERROR;
    if(EFI_ERROR(status)){GuFree(data);return status;}
    GuAssets[i].name=spec->name;GuAssets[i].firmware.data=data;GuAssets[i].firmware.size=spec->size;
    GuLog("asset verified: %s bytes=%zu sha256=%s\n",spec->name,(size_t)spec->size,spec->hash);
    if(!GuLogReady())return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
VOID GuReleaseAssets(VOID) {
  if(!GuSafeToReturn())return;
  for(UINTN i=0;i<5;i++){GuFree((VOID *)GuAssets[i].firmware.data);memset(&GuAssets[i],0,sizeof(GuAssets[i]));}
}
EFI_STATUS GuIoClose(VOID) {
  EFI_FILE_PROTOCOL **files[]={&Report,&Run,&Parent,&Root};EFI_STATUS result=EFI_SUCCESS;
  /* Land every batched log line before the safe-to-return gate: a retained
     or faulted state can reach this point without any Halt path, and the
     FINAL line must not be lost behind the early return. */
  GuLogFlush();
  if(!GuSafeToReturn())return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<4;i++)if(*files[i]) {
    /* EFI_FILE_PROTOCOL.Close invalidates the handle even on error. */
    EFI_FILE_PROTOCOL *file=*files[i];*files[i]=NULL;
    EFI_STATUS status=file->Close(file);
    if(EFI_ERROR(status) && !EFI_ERROR(result))result=status;
  }
  return result;
}
