/* Host unit test for machine_profile.txt parsing (GuLoadEspProfile in app_io.c).
   The real app_io.c is linked; only the EFI file handles are mocked. */
#include "../app_io.h"
#include "../port.h"
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern VOID GuIoTestSetParent(EFI_FILE_PROTOCOL *P);

static unsigned Checks;
static unsigned Failures;
static void CHECK(int Ok,const char *Name) {
  Checks++;
  if(!Ok){Failures++;printf("FAIL %s\n",Name);}
}

/* --- mocked file handle serving an in-memory byte buffer --- */
typedef struct {
  EFI_FILE_PROTOCOL Handle;
  const UINT8 *Data;
  UINTN Size,Pos;
  int Reads,GetInfos,Closes;
} FakeFile;
typedef struct {
  EFI_FILE_PROTOCOL Handle;
  FakeFile *File;
  int Opens,OpenFail;
} FakeDir;

static int wcmp(const CHAR16 *A,const CHAR16 *B) {
  while(*A && *A==*B){A++;B++;}
  return (int)*(UINT8 *)A-(int)*(UINT8 *)B;
}
static EFI_STATUS EFIAPI ff_read(EFI_FILE_PROTOCOL *This,UINTN *N,VOID *Buffer) {
  FakeFile *F=(FakeFile *)This;
  UINTN left=F->Size-F->Pos,copy=*N;
  F->Reads++;
  if(copy>left)copy=left;
  if(Buffer)memcpy(Buffer,F->Data+F->Pos,copy);
  F->Pos+=copy;*N=copy;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_getinfo(EFI_FILE_PROTOCOL *This,EFI_GUID *Type,UINTN *N,VOID *Buffer) {
  FakeFile *F=(FakeFile *)This;
  (void)Type;F->GetInfos++;
  if(*N<SIZE_OF_EFI_FILE_INFO){*N=SIZE_OF_EFI_FILE_INFO;return EFI_BUFFER_TOO_SMALL;}
  EFI_FILE_INFO *I=Buffer;
  *N=SIZE_OF_EFI_FILE_INFO;
  memset(I,0,SIZE_OF_EFI_FILE_INFO);
  I->Size=SIZE_OF_EFI_FILE_INFO;I->FileSize=F->Size;I->Attribute=0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_close(EFI_FILE_PROTOCOL *This) {
  ((FakeFile *)This)->Closes++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI fd_open(EFI_FILE_PROTOCOL *This,EFI_FILE_PROTOCOL **NH,CHAR16 *Name,UINT64 Mode,UINT64 Attr) {
  FakeDir *D=(FakeDir *)This;
  (void)Mode;(void)Attr;D->Opens++;
  if(D->OpenFail)return EFI_NOT_FOUND;
  if(wcmp(Name,L"machine_profile.txt")!=0)return EFI_NOT_FOUND;
  *NH=&D->File->Handle;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI fd_close(EFI_FILE_PROTOCOL *T) {(void)T;return EFI_SUCCESS;}

static FakeFile FF;
static FakeDir FD;
static EFI_BOOT_SERVICES Bs;
static EFI_STATUS EFIAPI pool(EFI_MEMORY_TYPE T,UINTN N,VOID **P) {
  (void)T;*P=calloc(1,N);return *P?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI freepool(VOID *P) {free(P);return EFI_SUCCESS;}

static EFI_STATUS run_parse(const void *Text,UINTN Len) {
  int open_fail=FD.OpenFail;
  memset(&FF,0,sizeof(FF));memset(&FD,0,sizeof(FD));
  FD.OpenFail=open_fail;
  FF.Data=Text;FF.Size=Len;
  FF.Handle.Revision=EFI_FILE_PROTOCOL_REVISION;
  FF.Handle.Close=ff_close;FF.Handle.Read=ff_read;FF.Handle.GetInfo=ff_getinfo;
  FD.File=&FF;
  FD.Handle.Revision=EFI_FILE_PROTOCOL_REVISION;
  FD.Handle.Open=fd_open;FD.Handle.Close=fd_close;
  memset(&Bs,0,sizeof(Bs));
  Bs.AllocatePool=pool;Bs.FreePool=freepool;
  GuInit(&Bs,NULL);
  GuIoTestSetParent(&FD.Handle);
  return GuLoadEspProfile();
}

static const EFI_GUID ExactGuid={0x12345678,0x9abc,0xdef0,
  {0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0}};

static void expect_ok(const char *Name,UINT64 Part,UINT64 Start,UINT64 Size,
                      UINT32 Cd,const EFI_GUID *G) {
  char label[128];
  snprintf(label,sizeof(label),"%s: loaded",Name);CHECK(Gu.esp_profile_loaded,label);
  snprintf(label,sizeof(label),"%s: partition",Name);CHECK(Gu.esp_partition==Part,label);
  snprintf(label,sizeof(label),"%s: start_lba",Name);CHECK(Gu.esp_start_lba==Start,label);
  snprintf(label,sizeof(label),"%s: size_lba",Name);CHECK(Gu.esp_size_lba==Size,label);
  snprintf(label,sizeof(label),"%s: countdown",Name);CHECK(Gu.countdown_seconds==Cd,label);
  snprintf(label,sizeof(label),"%s: guid",Name);CHECK(memcmp(&Gu.esp_guid,G,sizeof(Gu.esp_guid))==0,label);
}
int main(void) {
  /* The exact content written by tools/prepare_usb.py (LF, 152 bytes). */
  static const char Exact[] =
    "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
    "windows_esp_partition=1\n"
    "windows_esp_start_lba=2048\n"
    "windows_esp_size_lba=204800\n"
    "countdown_seconds=5\n";
  CHECK(run_parse(Exact,sizeof(Exact)-1)==EFI_SUCCESS,"exact: status");
  expect_ok("exact",1,2048,204800,5,&ExactGuid);

  /* CRLF line endings. */
  {
    char crlf[512];UINTN o=0;
    for(const char *p=Exact;*p;p++) {
      if(*p=='\n')crlf[o++]='\r';
      crlf[o++]=*p;
    }
    CHECK(run_parse(crlf,o)==EFI_SUCCESS,"crlf: status");
    expect_ok("crlf",1,2048,204800,5,&ExactGuid);
  }

  /* No trailing newline on the last line. */
  CHECK(run_parse(Exact,sizeof(Exact)-2)==EFI_SUCCESS,"no-final-nl: status");
  expect_ok("no-final-nl",1,2048,204800,5,&ExactGuid);

  /* Spaces around '=' and trailing spaces. */
  {
    static const char Spaces[] =
      "  windows_esp_guid = 12345678-9abc-def0-1234-56789abcdef0  \n"
      "windows_esp_partition= 1\n"
      "windows_esp_start_lba\t=\t2048\n"
      "windows_esp_size_lba=204800\n"
      "countdown_seconds=5\n";
    CHECK(run_parse(Spaces,sizeof(Spaces)-1)==EFI_SUCCESS,"spaces: status");
    expect_ok("spaces",1,2048,204800,5,&ExactGuid);
  }

  /* Uppercase hex GUID. */
  {
    static const char Upper[] =
      "windows_esp_guid=12345678-9ABC-DEF0-1234-56789ABCDEF0\n"
      "windows_esp_partition=1\n"
      "windows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\n";
    CHECK(run_parse(Upper,sizeof(Upper)-1)==EFI_SUCCESS,"upper-guid: status");
    expect_ok("upper-guid",1,2048,204800,5,&ExactGuid);
  }

  /* Countdown boundaries. */
  {
    char buf[512];
    snprintf(buf,sizeof(buf),
      "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
      "windows_esp_partition=1\nwindows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\ncountdown_seconds=%lu\n",(unsigned long)30);
    CHECK(run_parse(buf,strlen(buf))==EFI_SUCCESS,"cd30: status");
    expect_ok("cd30",1,2048,204800,30,&ExactGuid);
    snprintf(buf,sizeof(buf),
      "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
      "windows_esp_partition=1\nwindows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\ncountdown_seconds=3600\n");
    CHECK(run_parse(buf,strlen(buf))==EFI_SUCCESS,"cd3600: status");
    expect_ok("cd3600",1,2048,204800,3600,&ExactGuid);
    /* 0 is valid: no countdown, arm immediately. */
    snprintf(buf,sizeof(buf),
      "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
      "windows_esp_partition=1\nwindows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\ncountdown_seconds=0\n");
    CHECK(run_parse(buf,strlen(buf))==EFI_SUCCESS,"cd0: status");
    expect_ok("cd0",1,2048,204800,0,&ExactGuid);
    /* Out-of-range countdown is ignored, default 5 applies. */
    snprintf(buf,sizeof(buf),
      "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
      "windows_esp_partition=1\nwindows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\ncountdown_seconds=3601\n");
    CHECK(run_parse(buf,strlen(buf))==EFI_SUCCESS,"cd3601: status");
    expect_ok("cd3601",1,2048,204800,5,&ExactGuid);
  }

  /* Unknown keys are ignored. */
  {
    static const char Extra[] =
      "comment=ignored\n"
      "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
      "windows_esp_partition=1\n"
      "windows_esp_start_lba=2048\n"
      "windows_esp_size_lba=204800\n"
      "whatever=zzz\n";
    CHECK(run_parse(Extra,sizeof(Extra)-1)==EFI_SUCCESS,"extra-keys: status");
    expect_ok("extra-keys",1,2048,204800,5,&ExactGuid);
  }

  /* Missing each required key must be rejected. */
  {
    struct {const char *name;const char *text;} missing[]={
      {"missing-guid",
       "windows_esp_partition=1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"missing-partition",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"missing-start",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\nwindows_esp_partition=1\nwindows_esp_size_lba=204800\n"},
      {"missing-size",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\nwindows_esp_partition=1\nwindows_esp_start_lba=2048\n"},
    };
    for(unsigned i=0;i<sizeof(missing)/sizeof(missing[0]);i++) {
      char label[128];
      snprintf(label,sizeof(label),"%s: status",missing[i].name);
      CHECK(run_parse(missing[i].text,strlen(missing[i].text))==EFI_COMPROMISED_DATA,label);
      snprintf(label,sizeof(label),"%s: not loaded",missing[i].name);
      CHECK(!Gu.esp_profile_loaded,label);
    }
  }

  /* Malformed values must be rejected. */
  {
    struct {const char *name;const char *text;} bad[]={
      {"bad-guid-nonhex",
       "windows_esp_guid=zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz\n"
       "windows_esp_partition=1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"bad-guid-short",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef\n"
       "windows_esp_partition=1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"bad-guid-long",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef00\n"
       "windows_esp_partition=1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"bad-guid-partial",
       "windows_esp_guid=12zz5678-9abc-def0-1234-56789abcdef0\n"
       "windows_esp_partition=1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"bad-dec",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
       "windows_esp_partition=abc\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"negative-dec",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
       "windows_esp_partition=-1\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
      {"empty-value",
       "windows_esp_guid=12345678-9abc-def0-1234-56789abcdef0\n"
       "windows_esp_partition=\nwindows_esp_start_lba=2048\nwindows_esp_size_lba=204800\n"},
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
      char label[128];
      snprintf(label,sizeof(label),"%s: status",bad[i].name);
      CHECK(run_parse(bad[i].text,strlen(bad[i].text))==EFI_COMPROMISED_DATA,label);
      snprintf(label,sizeof(label),"%s: not loaded",bad[i].name);
      CHECK(!Gu.esp_profile_loaded,label);
    }
  }

  /* Empty file and oversized file. */
  CHECK(run_parse("",0)==EFI_COMPROMISED_DATA,"empty: status");
  CHECK(!Gu.esp_profile_loaded,"empty: not loaded");
  {
    char big[2048];
    memcpy(big,Exact,sizeof(Exact)-1);
    memset(big+sizeof(Exact)-1,'A',1025-(sizeof(Exact)-1));
    CHECK(run_parse(big,1025)==EFI_COMPROMISED_DATA,"oversize: status");
    CHECK(!Gu.esp_profile_loaded,"oversize: not loaded");
  }

  /* Open failure propagates and leaves no profile loaded. */
  FD.OpenFail=1;
  CHECK(run_parse(Exact,sizeof(Exact)-1)==EFI_NOT_FOUND,"open-fail: status");
  CHECK(!Gu.esp_profile_loaded,"open-fail: not loaded");
  FD.OpenFail=0;

  printf("%u checks, %u failures\n",Checks,Failures);
  if(Failures){puts("esp-profile parser: FAIL");return 1;}
  puts("esp-profile parser: PASS");
  return 0;
}
