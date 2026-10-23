/* Local modifications 2026-10-23; see CHANGES.md. Original notices retained. */
/* Local preparation only, 2026-10-23. GPL-2.0, like the surrounding port. */
#pragma once
#include "port.h"
#include "machine_profile.h" /* generated locally by build.ps1 */
typedef struct {
  EFI_HANDLE image;
  EFI_DEVICE_PATH_PROTOCOL *path;
} GU_WINDOWS_BOOT;
EFI_STATUS GuWindowsPrepare(EFI_HANDLE Parent, GU_WINDOWS_BOOT *Boot);
EFI_STATUS GuWindowsDiscard(GU_WINDOWS_BOOT *Boot);
EFI_STATUS GuWindowsStart(GU_WINDOWS_BOOT *Boot);
