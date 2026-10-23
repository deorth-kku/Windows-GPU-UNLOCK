/* Local modifications 2026-10-23; see CHANGES.md. Original notices retained. */
/* Exact local Windows ESP, never USB fallback or BootOrder rewriting.
   LoadImage only stages the loader; StartImage runs it after GPU cleanup.
   UEFI 2.10 sections 7.4 and 10.3.5.1. GPL-2.0. */
#include "windows_boot.h"
#include "compat/compat.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
static EFI_GUID WindowsEsp=GU_WINDOWS_ESP_GUID;
static EFI_GUID WindowsFsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID WindowsPathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID WindowsLoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const CHAR16 WindowsFile[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

/* Return the prefix length only for one bounded, single-instance, non-USB
   device path ending in the exact system GPT partition captured in Windows. */
static UINTN WindowsPartitionPrefix(const EFI_DEVICE_PATH_PROTOCOL *Path) {
  if(!Path)return 0;
  UINTN total=0;BOOLEAN matched=FALSE;
  for(UINTN nodes=0;nodes<64 && total+4<=4096;nodes++) {
    UINTN n=Path->Length[0]+((UINTN)Path->Length[1]<<8);
    if(n<4 || n>4096-total)return 0;
    if(Path->Type==END_DEVICE_PATH_TYPE)
      return Path->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && n==4 && matched?total:0;
    if(matched)return 0; /* HD must be the final non-end node. */
    if(Path->Type==MESSAGING_DEVICE_PATH &&
       (Path->SubType==MSG_USB_DP || Path->SubType==MSG_USB_CLASS_DP || Path->SubType==MSG_USB_WWID_DP))return 0;
    if(Path->Type==MEDIA_DEVICE_PATH) {
      if(Path->SubType!=MEDIA_HARDDRIVE_DP || n!=sizeof(HARDDRIVE_DEVICE_PATH))return 0;
      HARDDRIVE_DEVICE_PATH hd;memcpy(&hd,Path,sizeof(hd));
      if(hd.MBRType!=MBR_TYPE_EFI_PARTITION_TABLE_HEADER || hd.SignatureType!=SIGNATURE_TYPE_GUID ||
         hd.PartitionNumber!=GU_WINDOWS_ESP_PARTITION || hd.PartitionStart!=GU_WINDOWS_ESP_START_LBA || hd.PartitionSize!=GU_WINDOWS_ESP_SIZE_LBA ||
         memcmp(hd.Signature,&WindowsEsp,sizeof(WindowsEsp)))return 0;
      matched=TRUE;
    }
    total+=n;Path=(const VOID *)((const UINT8 *)Path+n);
  }
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
    if(EFI_ERROR(status))return status;
    Boot->image=NULL;
  }
  if(Boot->path) {
    EFI_STATUS status=Gu.bs->FreePool(Boot->path);
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
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&WindowsFsGuid,NULL,&count,&handles);
  if(EFI_ERROR(status) || !handles || !count || count>256) {
    if(handles)Gu.bs->FreePool(handles);
    return EFI_ERROR(status)?status:EFI_NOT_FOUND;
  }
  for(UINTN i=0;i<count;i++) {
    EFI_DEVICE_PATH_PROTOCOL *candidate=NULL;
    if(EFI_ERROR(Gu.bs->HandleProtocol(handles[i],&WindowsPathGuid,(VOID **)&candidate)))continue;
    UINTN bytes=WindowsPartitionPrefix(candidate);
    if(bytes){matches++;prefix=bytes;partition=handles[i];path=candidate;}
  }
  status=Gu.bs->FreePool(handles);if(EFI_ERROR(status))return status;
  if(matches!=1 || !partition || !prefix)return EFI_NOT_FOUND;
  UINTN file_bytes=4+sizeof(WindowsFile),size=prefix+file_bytes+4;
  status=Gu.bs->AllocatePool(EfiLoaderData,size,(VOID **)&Boot->path);
  if(EFI_ERROR(status) || !Boot->path)return EFI_ERROR(status)?status:EFI_OUT_OF_RESOURCES;
  memcpy(Boot->path,path,prefix);
  UINT8 *file=(UINT8 *)Boot->path+prefix;
  file[0]=MEDIA_DEVICE_PATH;file[1]=MEDIA_FILEPATH_DP;
  file[2]=(UINT8)file_bytes;file[3]=(UINT8)(file_bytes>>8);
  memcpy(file+4,WindowsFile,sizeof(WindowsFile));
  file+=file_bytes;file[0]=END_DEVICE_PATH_TYPE;file[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;file[2]=4;file[3]=0;
  status=Gu.bs->LoadImage(FALSE,Parent,Boot->path,NULL,0,&Boot->image);
  /* A security error can still return an image handle. Discard it on error. */
  if(!EFI_ERROR(status) && !Boot->image)status=EFI_LOAD_ERROR;
  if(!EFI_ERROR(status)) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
    status=Gu.bs->HandleProtocol(Boot->image,&WindowsLoadedGuid,(VOID **)&loaded);
    if(!EFI_ERROR(status) && (!loaded || loaded->DeviceHandle!=partition || !WindowsLoadedFile(loaded->FilePath)))
      status=EFI_COMPROMISED_DATA;
  }
  if(EFI_ERROR(status)) {
    EFI_STATUS discarded=GuWindowsDiscard(Boot);
    return EFI_ERROR(discarded)?discarded:status;
  }
  return EFI_SUCCESS;
}
EFI_STATUS GuWindowsStart(GU_WINDOWS_BOOT *Boot) {
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
