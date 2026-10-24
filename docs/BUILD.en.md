# Build and Verification

Python 3.9+, LLVM/clang + lld-link are required. The actual EFI was built with **clang 20.1.8**. The default build is a read-only preflight and does not perform the unlock.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Clang C:\TOOLS\LLVM20\bin\clang.exe `
  -LldLink C:\TOOLS\LLVM20\bin\lld-link.exe `
  -ExperimentalUnlock -StockProfile entryLatchedSs1Full
```

Output:

```text
unlock/build/experimental/entryLatchedSs1Full/RTX3090-EXPERIMENTAL-entryLatchedSs1Full.EFI
```

The build also verifies the PE/COFF structure. During public preparation, a rebuild with clang 20.1.8 matched the SHA-256 of the successful USB EFI. Compare your new build with `Get-FileHash`. `HardwareTested=false` in a new build means the automated build process did not run on real hardware. Success records are kept in a separate `evidence/` directory.

## Autostart (automatic Windows handoff) build

The autostart variant is built by supplying a simple key=value text file via `-MachineProfile`. This file carries the local Windows partition identity and countdown duration, and is gitignored.

```text
# unlock/machine_profile.txt
windows_esp_guid=ce842bcf-937d-4e0e-a191-95568502f089
windows_esp_partition=1
windows_esp_start_lba=616448
windows_esp_size_lba=3998228480
countdown_seconds=5
```

| Key | Description |
|---|---|
| `windows_esp_guid` | GPT unique identifier (GUID) of the Windows system partition |
| `windows_esp_partition` | GPT partition number (1-based) |
| `windows_esp_start_lba` | Partition start LBA |
| `windows_esp_size_lba` | Partition size in LBA |
| `countdown_seconds` | Countdown duration in seconds (default 5; `0` arms immediately with no countdown) |

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Clang C:\TOOLS\LLVM20\bin\clang.exe `
  -LldLink C:\TOOLS\LLVM20\bin\lld-link.exe `
  -ExperimentalUnlock -StockProfile entryLatchedSs1Preserve `
  -MachineProfile machine_profile.txt
```

Output:

```text
unlock/build/autostart/RTX3090-AUTOLOAD.EFI
```

`machine_profile.txt` is generated automatically by `tools/prepare_usb.py --autostart` during USB packaging. It collects the partition GUID, number, start LBA, and size via PowerShell `Get-Partition -DriveLetter C`.

## Host mock tests

The tests do not access the GPU. An MSVC/Windows SDK environment is required. If the default VS path differs, specify `-VcVars`. The full test suite below uses local fixtures on the owned PC; PROM, original captures, and firmware are not published.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1 `
  -Clang C:\TOOLS\LLVM20\bin\clang.exe `
  -VcVars 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat' `
  -WithLocalFixtures -ExperimentalUnlock -StockProfile entryLatchedSs1Full `
  -Assets C:\LOCAL-FIXTURES\assets `
  -Prom C:\LOCAL-FIXTURES\probe\PROM.BIN `
  -Capture C:\LOCAL-FIXTURES\probe\REPORT.TXT `
  -ColdCapture C:\LOCAL-FIXTURES\cold\REPORT.TXT
```

Do not represent missing fixtures or partial validation as a full pass. Legacy `test-*.cmd` and some historical tests assume the original development environment; the entry point for the current port is `test-all.ps1`. Mock passes are not evidence of real GPU success or long-term stability.

## Verification for this public release

During the 2026-10-20 public preparation, the **10 host mock groups and the malformed PE rejection test** for the corresponding profile were re-run and passed. The [test results](../evidence/host-tests.json) state `HardwareAccess=false`. Five packaging-tool tests (bad asset, modified source, existing destination, path traversal rejection, and normal packaging) also passed.

```powershell
python -m unittest discover -s tools -p test_prepare_usb.py -v
```
