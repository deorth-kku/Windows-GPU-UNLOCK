/* Local modifications 2026-10-23; see CHANGES.md. Original notices retained. */
/* Exact local Windows ESP, never USB fallback or BootOrder rewriting.
   LoadImage only stages the loader; StartImage runs it after GPU cleanup.
   UEFI 2.10 sections 7.4 and 10.3.5.1. GPL-2.0. */
#include "windows_boot.h"
#include "compat/compat.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
static EFI_GUID WindowsFsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID WindowsPathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID WindowsLoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const CHAR16 WindowsFile[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static VOID WindowsGuidLog(const char *Tag,const EFI_GUID *Guid) {
  const UINT8 *b=(const UINT8 *)Guid;
  GuLog("%s=%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",Tag,
    b[3],b[2],b[1],b[0],b[5],b[4],b[7],b[6],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}
static VOID WindowsHdLog(const char *Tag,const char *SigTag,const HARDDRIVE_DEVICE_PATH *Hd) {
  GuLog("%s: MBRType=%u SignatureType=%u PartitionNumber=%u PartitionStart=%llu PartitionSize=%llu\n",Tag,
    (unsigned)Hd->MBRType,(unsigned)Hd->SignatureType,(unsigned)Hd->PartitionNumber,
    (unsigned long long)Hd->PartitionStart,(unsigned long long)Hd->PartitionSize);
  WindowsGuidLog(SigTag,(const EFI_GUID *)Hd->Signature);
}
/* Return the prefix length only for one bounded, single-instance, non-USB
   device path ending in the exact system GPT partition captured in Windows. */
static UINTN WindowsPartitionPrefix(const EFI_DEVICE_PATH_PROTOCOL *Path) {
  if(!Path){GuLog("windows-loader: candidate path is NULL\n");return 0;}
  if(!Gu.esp_profile_loaded){GuLog("windows-loader: reject: esp profile not loaded\n");return 0;}
  GuLog("windows-loader: expected HD node MBRType=%u SignatureType=%u PartitionNumber=%llu PartitionStart=%llu PartitionSize=%llu\n",
    (unsigned)MBR_TYPE_EFI_PARTITION_TABLE_HEADER,(unsigned)SIGNATURE_TYPE_GUID,
    (unsigned long long)Gu.esp_partition,(unsigned long long)Gu.esp_start_lba,(unsigned long long)Gu.esp_size_lba);
  WindowsGuidLog("windows-loader: expected Signature",&Gu.esp_guid);
  UINTN total=0;BOOLEAN matched=FALSE;
  for(UINTN nodes=0;nodes<64 && total+4<=4096;nodes++) {
    UINTN n=Path->Length[0]+((UINTN)Path->Length[1]<<8);
    GuLog("windows-loader: node[%u] Type=%u SubType=%u len=%u total=%u\n",
      (unsigned)nodes,(unsigned)Path->Type,(unsigned)Path->SubType,(unsigned)n,(unsigned)total);
    if(n<4 || n>4096-total){GuLog("windows-loader: node[%u] rejected: bad length\n",(unsigned)nodes);return 0;}
    if(Path->Type==END_DEVICE_PATH_TYPE) {
      GuLog("windows-loader: END node SubType=%u len=%u matched=%u -> %s\n",
        (unsigned)Path->SubType,(unsigned)n,(unsigned)matched,
        Path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && n==4 && matched?"ACCEPT":"reject");
      return Path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && n==4 && matched?total:0;
    }
    if(matched){GuLog("windows-loader: node[%u] rejected: node after HD\n",(unsigned)nodes);return 0;}
    if(Path->Type==MESSAGING_DEVICE_PATH &&
       (Path->SubType==MSG_USB_DP || Path->SubType==MSG_USB_CLASS_DP || Path->SubType==MSG_USB_WWID_DP)) {
      GuLog("windows-loader: node[%u] rejected: USB node\n",(unsigned)nodes);return 0;
    }
    if(Path->Type==MEDIA_DEVICE_PATH) {
      if(Path->SubType!=MEDIA_HARDDRIVE_DP || n!=sizeof(HARDDRIVE_DEVICE_PATH)) {
        GuLog("windows-loader: node[%u] rejected: media SubType=%u len=%u (need HD len=%u)\n",
          (unsigned)nodes,(unsigned)Path->SubType,(unsigned)n,(unsigned)sizeof(HARDDRIVE_DEVICE_PATH));
        return 0;
      }
      HARDDRIVE_DEVICE_PATH hd;memcpy(&hd,Path,sizeof(hd));
      WindowsHdLog("windows-loader: HD node","windows-loader: HD Signature",&hd);
      if(hd.MBRType!=MBR_TYPE_EFI_PARTITION_TABLE_HEADER || hd.SignatureType!=SIGNATURE_TYPE_GUID ||
         hd.PartitionNumber!=Gu.esp_partition || hd.PartitionStart!=Gu.esp_start_lba || hd.PartitionSize!=Gu.esp_size_lba ||
         memcmp(hd.Signature,&Gu.esp_guid,sizeof(Gu.esp_guid))) {
        GuLog("windows-loader: HD node rejected: field mismatch\n");return 0;
      }
      matched=TRUE;
    }
    total+=n;Path=(const VOID *)((const UINT8 *)Path+n);
  }
  GuLog("windows-loader: path exhausted without END/match\n");
  return 0;
}
static BOOLEAN WindowsLoadedFile(const EFI_DEVICE_PATH_PROTOCOL *Path) {
  if(!Path || Path->Type!=MEDIA_DEVICE_PATH || Path->SubType!=MEDIA_FILEPATH_DP)return FALSE;
  const UINTN bytes=4+sizeof(WindowsFile);
  UINTN n=Path->Length[0]+((UINTN)Path->Length[1]<<8);
  if(n!=bytes || memcmp((const UINT8 *)Path+4,WindowsFile,sizeof(WindowsFile)))return FALSE;
  Path=(const VOID *)((const UINT8 *)Path+n);
  return Path->Type==END_DEVICE_PATH_TYPE && Path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE &&
    Path->Length[0]==4 && !Path->Length[1];
}
EFI_STATUS GuWindowsDiscard(GU_WINDOWS_BOOT *Boot) {
  if(!Boot)return EFI_INVALID_PARAMETER;
  if(Boot->image) {
    EFI_STATUS status=Gu.bs->UnloadImage(Boot->image);
    GuLog("windows-loader: UnloadImage status=%#llx\n",(unsigned long long)status);
    if(EFI_ERROR(status))return status;
    Boot->image=NULL;
  }
  if(Boot->path) {
    EFI_STATUS status=Gu.bs->FreePool(Boot->path);
    GuLog("windows-loader: FreePool(path) status=%#llx\n",(unsigned long long)status);
    if(EFI_ERROR(status))return status;
    Boot->path=NULL;
  }
  return EFI_SUCCESS;
}
EFI_STATUS GuWindowsPrepare(EFI_HANDLE Parent, GU_WINDOWS_BOOT *Boot) {
  if(!Boot || Boot->image || Boot->path || !Parent || !Gu.bs || !Gu.bs->LoadImage ||
     !Gu.bs->StartImage || !Gu.bs->UnloadImage || !Gu.bs->HandleProtocol ||
     !Gu.bs->LocateHandleBuffer || !Gu.bs->AllocatePool || !Gu.bs->FreePool)return EFI_INVALID_PARAMETER;
  EFI_HANDLE *handles=NULL,partition=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  UINTN count=0,prefix=0,matches=0;
  GuLog("windows-loader: expected file \\EFI\\Microsoft\\Boot\\bootmgfw.efi\n");
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&WindowsFsGuid,NULL,&count,&handles);
  GuLog("windows-loader: SimpleFileSystem handles: status=%#llx count=%u\n",
    (unsigned long long)status,(unsigned)count);
  if(EFI_ERROR(status) || !handles || !count || count>256) {
    if(handles)Gu.bs->FreePool(handles);
    GuLog("windows-loader: reject: no SimpleFileSystem handle set (status=%#llx count=%u)\n",
      (unsigned long long)status,(unsigned)count);
    return EFI_ERROR(status)?status:EFI_NOT_FOUND;
  }
  for(UINTN i=0;i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *candidate=NULL;
    EFI_STATUS pstatus=Gu.bs->HandleProtocol(handles[i],&WindowsPathGuid,(VOID **)&candidate);
    GuLog("windows-loader: candidate[%u] DevicePath status=%#llx path=%#llx\n",
      (unsigned)i,(unsigned long long)pstatus,(unsigned long long)(const VOID *)candidate);
    if(EFI_ERROR(pstatus))continue;
    UINTN bytes=WindowsPartitionPrefix(candidate);
    GuLog("windows-loader: candidate[%u] prefix=%u\n",(unsigned)i,(unsigned)bytes);
    if(bytes){matches++;prefix=bytes;partition=handles[i];path=candidate;}
  }
  status=Gu.bs->FreePool(handles);if(EFI_ERROR(status))return status;
  GuLog("windows-loader: matching partitions=%u prefix=%u\n",(unsigned)matches,(unsigned)prefix);
  if(matches!=1 || !partition || !prefix) {
    GuLog("windows-loader: reject: need exactly one matching partition (matches=%u)\n",(unsigned)matches);
    return EFI_NOT_FOUND;
  }
  UINTN file_bytes=4+sizeof(WindowsFile),size=prefix+file_bytes+4;
  status=Gu.bs->AllocatePool(EfiLoaderData,size,(VOID **)&Boot->path);
  GuLog("windows-loader: AllocatePool size=%u status=%#llx path=%#llx\n",
    (unsigned)size,(unsigned long long)status,(unsigned long long)(const VOID *)Boot->path);
  if(EFI_ERROR(status) || !Boot->path)return EFI_ERROR(status)?status:EFI_OUT_OF_RESOURCES;
  memcpy(Boot->path,path,prefix);
  UINT8 *file=(UINT8 *)Boot->path+prefix;
  file[0]=MEDIA_DEVICE_PATH;file[1]=MEDIA_FILEPATH_DP;
  file[2]=(UINT8)file_bytes;file[3]=(UINT8)(file_bytes>>8);
  memcpy(file+4,WindowsFile,sizeof(WindowsFile));
  file+=file_bytes;file[0]=END_DEVICE_PATH_TYPE;file[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;file[2]=4;file[3]=0;
  status=Gu.bs->LoadImage(FALSE,Parent,Boot->path,NULL,0,&Boot->image);
  GuLog("windows-loader: LoadImage status=%#llx image=%#llx\n",
    (unsigned long long)status,(unsigned long long)(const VOID *)Boot->image);
  /* A security error can still return an image handle. Discard it on error. */
  if(!EFI_ERROR(status) && !Boot->image)status=EFI_LOAD_ERROR;
  if(!EFI_ERROR(status)) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
    status=Gu.bs->HandleProtocol(Boot->image,&WindowsLoadedGuid,(VOID **)&loaded);
    GuLog("windows-loader: LoadedImage status=%#llx loaded=%#llx device=%#llx file_path=%#llx\n",
      (unsigned long long)status,(unsigned long long)(const VOID *)loaded,
      (unsigned long long)(loaded?loaded->DeviceHandle:NULL),
      (unsigned long long)(loaded?loaded->FilePath:NULL));
    if(!EFI_ERROR(status)) {
      BOOLEAN same_device=loaded && loaded->DeviceHandle==partition;
      BOOLEAN good_file=loaded && WindowsLoadedFile(loaded->FilePath);
      GuLog("windows-loader: verify device_match=%u file_match=%u\n",
        (unsigned)same_device,(unsigned)good_file);
      if(!same_device || !good_file)status=EFI_COMPROMISED_DATA;
    }
  }
  if(EFI_ERROR(status)) {
    EFI_STATUS discarded=GuWindowsDiscard(Boot);
    GuLog("windows-loader: prepare failed status=%#llx discard=%#llx\n",
      (unsigned long long)status,(unsigned long long)discarded);
    return EFI_ERROR(discarded)?discarded:status;
  }
  GuLog("windows-loader: prepared; loader staged, start deferred until after cleanup\n");
  return EFI_SUCCESS;
}
EFI_STATUS GuWindowsStart(GU_WINDOWS_BOOT *Boot) {
  GuLog("windows-loader: start: image=%#llx path=%#llx safe=%u armed=%u owned=%u allocs=%#llx dma=%u\n",
    (unsigned long long)(const VOID *)(Boot?Boot->image:NULL),
    (unsigned long long)(const VOID *)(Boot?Boot->path:NULL),(unsigned)GuSafeToReturn(),
    (unsigned)Gu.armed,(unsigned)Gu.owned,(unsigned long long)(const VOID *)Gu.allocations,(unsigned)Gu.dma_count);
  if(!Boot || !Boot->image || !Boot->path || !GuSafeToReturn() || Gu.armed || Gu.owned ||
     Gu.allocations || Gu.dma_count)return EFI_ACCESS_DENIED;
  EFI_STATUS status=Gu.bs->FreePool(Boot->path);
  if(EFI_ERROR(status))return status;
  Boot->path=NULL;
  EFI_HANDLE image=Boot->image;Boot->image=NULL;
  /* Called once only. A returning UEFI application has already been unloaded
     by StartImage. No reset, alternate loader, BootNext, or retry here. */
  return Gu.bs->StartImage(image,NULL,NULL);
}
