# 빌드와 검사

Python 3.9+, LLVM/clang + lld-link가 필요합니다. 실제 EFI는 **clang 20.1.8**로 빌드했습니다. 기본 빌드는 무쓰기 preflight이며 언락을 실행하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Clang C:\TOOLS\LLVM20\bin\clang.exe `
  -LldLink C:\TOOLS\LLVM20\bin\lld-link.exe `
  -ExperimentalUnlock -StockProfile entryLatchedSs1Full
```

생성물:

```text
unlock/build/experimental/entryLatchedSs1Full/RTX3090-EXPERIMENTAL-entryLatchedSs1Full.EFI
```

## Autostart (자동 부팅 인계) 빌드

Autostart 변형은 `-MachineProfile`에 simple key=value 텍스트 파일을 지정해 빌드합니다. 이 파일은 로컬 Windows 파티션 식별자와 카운트다운 시간을 담으며 gitignore 대상입니다.

```text
# unlock/machine_profile.txt
windows_esp_guid=ce842bcf-937d-4e0e-a191-95568502f089
windows_esp_partition=1
windows_esp_start_lba=616448
windows_esp_size_lba=3998228480
countdown_seconds=5
```

| 키 | 설명 |
|---|---|
| `windows_esp_guid` | Windows 시스템 파티션의 GPT unique identifier (GUID) |
| `windows_esp_partition` | GPT 파티션 번호 (1-based) |
| `windows_esp_start_lba` | 파티션 시작 LBA |
| `windows_esp_size_lba` | 파티션 크기 (LBA) |
| `countdown_seconds` | 카운트다운 초 (기본 5; `0`이면 카운트다운 없이 즉시 실행) |

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Clang C:\TOOLS\LLVM20\bin\clang.exe `
  -LldLink C:\TOOLS\LLVM20\bin\lld-link.exe `
  -ExperimentalUnlock -StockProfile entryLatchedSs1Preserve `
  -MachineProfile machine_profile.txt
```

생성물:

```text
unlock/build/autostart/RTX3090-AUTOLOAD.EFI
```

`machine_profile.txt`는 `tools/prepare_usb.py --autostart`로 USB 패키징 시 자동 생성됩니다. PowerShell `Get-Partition -DriveLetter C`에서 파티션 GUID·번호·시작 LBA·크기를 수집해 파일에 기록합니다.

빌드는 PE/COFF 구조도 검사합니다. 공개 준비 과정에서 clang 20.1.8로 재빌드했고 실제 성공한 USB EFI와 SHA-256이 일치했습니다. 사용자의 새 빌드는 `Get-FileHash`로 비교하세요. 새 빌드의 `HardwareTested=false`는 자동 빌드 과정이 실제 하드웨어 시험을 수행하지 않았다는 뜻입니다. 성공 기록은 별도 `evidence/`에 둡니다.

## Host mock 검사

검사 자체는 GPU에 접근하지 않습니다. MSVC/Windows SDK 환경이 필요합니다. 기본 VS 경로가 다르면 `-VcVars`를 지정합니다. 아래 전체 검사는 소유한 PC의 로컬 fixture를 사용하며 PROM·원본 캡처·펌웨어는 공개하지 않습니다.

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

fixture 누락 또는 부분 검증을 완전 통과로 표현하면 안 됩니다. 기존 `test-*.cmd`와 일부 역사적 테스트는 원본 개발 환경을 전제로 하므로 현재 포트의 진입점은 `test-all.ps1`입니다. mock 통과는 실제 GPU 성공이나 장기 안정성의 증거가 아닙니다.

## 이번 공개본 검증

2026-10-20 공개 준비 과정에서 해당 프로필의 host mock **10개 그룹과 잘못된 PE 거부 검사**를 다시 실행해 통과했습니다. [검사 결과](../evidence/host-tests.json)는 `HardwareAccess=false`를 명시합니다. 패키징 도구의 잘못된 자산·수정 소스·기존 대상·경로 이탈 거부 및 정상 패키징 5개 테스트도 통과했습니다.

```powershell
python -m unittest discover -s tools -p test_prepare_usb.py -v
```
