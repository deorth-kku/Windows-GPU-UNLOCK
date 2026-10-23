# Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md.
param(
 [string]$Clang='clang',
 [string]$LldLink='lld-link',
 [string]$Python='python',
 [switch]$ExperimentalUnlock,
 [ValidateSet('original03100062','observed03000062','entryLatchedSs1Preserve','entryLatchedSs1Full')][string]$StockProfile,
 [Guid]$WindowsEspGuid,
 [UInt64]$WindowsEspStartLba,
 [UInt64]$WindowsEspSizeLba,
 [UInt32]$WindowsEspPartition=1
)
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 if(!(Test-Path -LiteralPath $PSScriptRoot)){throw 'Missing output parent'}
 if($ExperimentalUnlock -and !$PSBoundParameters.ContainsKey('StockProfile')){throw '-StockProfile is required with -ExperimentalUnlock'}
 if(!$ExperimentalUnlock -and $PSBoundParameters.ContainsKey('StockProfile')){throw '-StockProfile is only valid with -ExperimentalUnlock'}
 $autostart=$PSBoundParameters.ContainsKey('WindowsEspGuid') -and $PSBoundParameters.ContainsKey('WindowsEspStartLba') -and $PSBoundParameters.ContainsKey('WindowsEspSizeLba')
 if($autostart){
  if(-not $ExperimentalUnlock){throw 'Windows ESP profile requires -ExperimentalUnlock'}
  if($StockProfile -ne 'entryLatchedSs1Preserve'){throw 'Autostart requires -StockProfile entryLatchedSs1Preserve'}
  if($WindowsEspGuid -eq [Guid]::Empty -or -not $WindowsEspStartLba -or -not $WindowsEspSizeLba){throw 'Explicit non-empty Windows ESP profile required'}
 }
 $out=if($autostart){'build/autostart'}elseif($ExperimentalUnlock){"build/experimental/$StockProfile"}else{'build'}
 $name=if($autostart){'RTX3090-AUTOLOAD'}elseif($ExperimentalUnlock){"RTX3090-EXPERIMENTAL-$StockProfile"}else{'RTX3090-PREFLIGHT'}
 $defines=@()
  if($ExperimentalUnlock){$defines+= '-DGU_EXPERIMENTAL_3090';$defines+=$(switch($StockProfile){'original03100062'{'-DGU_STOCK_PROFILE_ORIGINAL_03100062'} 'observed03000062'{'-DGU_STOCK_PROFILE_OBSERVED_03000062'} 'entryLatchedSs1Preserve'{'-DGU_ENTRY_LATCHED_SS1_PRESERVE'} 'entryLatchedSs1Full'{'-DGU_ENTRY_LATCHED_SS1_FULL'}})}
  if($autostart){$defines+= '-DGU_WINDOWS_HANDOFF'}
 New-Item -ItemType Directory -Path $out -Force | Out-Null
 if($autostart){
  $guidBytes=$WindowsEspGuid.ToByteArray()
  $guidTail=($guidBytes[8..15] | ForEach-Object {'0x{0:x2}' -f $_}) -join ','
  $guidInit='{0x'+$WindowsEspGuid.ToString('N').Substring(0,8)+',0x'+$WindowsEspGuid.ToString('N').Substring(8,4)+',0x'+$WindowsEspGuid.ToString('N').Substring(12,4)+',{'+$guidTail+'}}'
  @('#pragma once',"#define GU_WINDOWS_ESP_GUID $guidInit","#define GU_WINDOWS_ESP_PARTITION $($WindowsEspPartition)ULL","#define GU_WINDOWS_ESP_START_LBA $($WindowsEspStartLba)ULL","#define GU_WINDOWS_ESP_SIZE_LBA $($WindowsEspSizeLba)ULL") | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'machine_profile.h') -Encoding ascii
 }
 $sources=@('app','app_io','ownership','format','runtime','sha256','checks','port','compat',
  'vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval')
 if($autostart){$sources=@('app','windows_boot','app_io','ownership','format','runtime','sha256','checks','port','compat',
  'vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval')}
 $objects=@()
 foreach($source in $sources) {
  $object="$out/"+($source -replace '/','_')+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220810deU @defines -I compat -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"};$objects+=$object
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf "/map:$out/$name.map" "/out:$out/$name.EFI" @objects
 if($LASTEXITCODE -ne 0){throw 'EFI link failed'}
 & $Python verify_pe.py "$out/$name.EFI" "$out/$name.map" --output "$out/pe-verification.json"
 if($LASTEXITCODE -ne 0){throw 'PE verification failed'}
 [ordered]@{BuiltUtc=[DateTime]::UtcNow.ToString('o');TargetPci='220410de';Subsystem='145410de';
  Mode=$(if($autostart){'local-autostart-windows-handoff'}elseif($ExperimentalUnlock){'experimental-exact-card-manual-unlock'}else{'diagnostic-preflight-only'});
  CanArm=[bool]$ExperimentalUnlock;Defines=$defines;ManualConfirmation=$(if($autostart){'5-second countdown; Esc skips unlock and boots Windows'}else{'U once; Esc or 30-second timeout cancels'});
  WindowsHandoff=[bool]$autostart;CountdownSeconds=$(if($autostart){5}else{$null});WindowsEspGuid=$(if($autostart){$WindowsEspGuid.ToString()}else{$null});WindowsEspPartition=$(if($autostart){$WindowsEspPartition}else{$null});
  StockProfile=$(if($ExperimentalUnlock){$StockProfile}else{$null});ColdChipExpected='0xb72000a1';ColdSS0Expected=$(if($StockProfile -like 'entryLatchedSs1*'){'first fully validated cold entry; immutable'}elseif($ExperimentalUnlock){if($StockProfile -eq 'original03100062'){'0x03100062'}else{'0x03000062'}}else{'unknown'});ColdSS1Expected=$(if($StockProfile -like 'entryLatchedSs1*'){'first fully validated cold entry <= 0xf; immutable'}else{'0x6'});
  SS0Target='0x88888888';SS1Target=$(if($StockProfile -eq 'entryLatchedSs1Preserve'){'preserve entry; no SS1 writes'}elseif($StockProfile -eq 'entryLatchedSs1Full'){'0x8; one guarded write'}elseif($ExperimentalUnlock){'0x8'}else{'no writes'});
  OriginalBaselineCapture='G:/RTX3090-PREFLIGHT/RUN00/REPORT.TXT';
  StockProfileSource=$(if($StockProfile -like 'entryLatchedSs1*'){'Explicit immutable validated entry latch; no fixed stock selector assumption'}elseif($StockProfile -eq 'observed03000062'){'Explicit observed03000062 experiment selection; original baseline capture does not evidence this SS0'}elseif($ExperimentalUnlock){'OriginalBaselineCapture'}else{'preflight has no selected stock profile'});
  PromSha256='8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4';
  DeviceMemorySize='0x6000';HardwareTested=$false;
  EfiSha256=(Get-FileHash -LiteralPath "$out/$name.EFI" -Algorithm SHA256).Hash;
  Headers=@(Get-ChildItem -LiteralPath . -Recurse -File -Filter '*.h' | Where-Object {$_.FullName -notlike '*\build\*'} | Sort-Object FullName | ForEach-Object {[ordered]@{Path=$_.FullName.Substring($PSScriptRoot.Length+1);Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}});
  Sources=@($sources | ForEach-Object {[ordered]@{Path="$_.c";Sha256=(Get-FileHash -LiteralPath "$_.c" -Algorithm SHA256).Hash}})
  } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath "$out/build-manifest.json"
 "Built $name.EFI; ExperimentalUnlock=$([bool]$ExperimentalUnlock); hardware untested."
} finally {Pop-Location}
