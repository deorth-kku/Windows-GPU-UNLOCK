# USB 부팅부터 Windows 측정까지

## 1. 적용 범위와 준비

[지원 조건](COMPATIBILITY.ko.md)에 맞는지 먼저 확인합니다. 이 공개본의 실행 검사는 실제 검증한 PC에 고정되어 있습니다. 다른 시스템을 지원하려면 PCI/MMIO/DMA·GOP·PROM·메모리 배치 검증을 포함한 별도 포팅이 필요합니다.

UEFI 앱은 GPU 상태를 변경하므로 진행 중 오류에 대비해 작업을 저장하세요. Secure Boot 설정을 바꾸기 전에 BitLocker/장치 암호화를 사용하는 PC는 복구 키를 확인합니다. 현재 Windows 디스크의 EFI·복구 파티션은 건드리지 않습니다.

- 화면 출력 케이블은 메인보드의 내장그래픽 출력에 연결합니다.
- RTX 3090의 **보조 전원 케이블과 PCIe 장착은 유지**합니다. '케이블을 뺀다'는 말은 RTX의 모니터 출력 연결을 뜻합니다.
- 내장그래픽으로 BIOS와 Windows가 정상 출력되는지 확인합니다.
- x64 UEFI 부팅, Secure Boot 꺼짐을 확인합니다. 설정 변경·저장이 필요하면 언락 실행 전에 끝냅니다.
- 충분한 공간이 있는 FAT32 USB를 준비하고 기존 파일을 별도 백업합니다. 이 프로젝트의 패키징 도구는 USB를 포맷하거나 디스크를 축소하지 않습니다.

## 2. 실행 파일과 자산 준비

저장소 루트에서 펌웨어를 `assets/`에 준비한 뒤 실행합니다.

```powershell
python .\tools\prepare_usb.py --assets .\assets --output .\usb-package
```

해시·크기 검증을 통과하면 새 로컬 폴더가 생깁니다. `usb-package` **안의 내용**을 FAT32 USB 최상위로 복사합니다. 기존 대상 파일을 덮어쓸 필요가 있으면 먼저 백업하고 내용과 대상을 확인합니다.

```text
USB:/
  EFI/BOOT/BOOTX64.EFI
  RTX3090-UNLOCK/
    ASSETS/
      booter_load_ga102.bin
      v67-ga102-610.43.03.bin
      gsp_ga10x.bin
      gsp_boot_ga102.bin
      gsp_boot_desc_ga102.bin
    LICENSES/
    SOURCE.zip
  USB-README.md
```

`BOOTX64.EFI`만 복사하면 실행되지 않습니다. `RTX3090-UNLOCK` 폴더는 로그를 쓸 수 있어야 합니다.

## 3. 언락 실행

1. ComfyUI 등 작업과 결과를 저장합니다. Windows에서 Shift를 누른 채 종료해 완전히 종료한 뒤 전원을 켭니다. 이미 드라이버가 사용한 warm 상태에서는 검사가 거부될 수 있습니다.
2. ASUS 로고에서 F8 → **UEFI USB**를 선택합니다.
3. 프로필 `entryLatchedSs1Full`과 사전 검사 통과를 확인합니다.
4. **U를 한 번** 누릅니다. Esc 또는 확인 없이 30초 대기는 취소입니다.
5. 실행 중 USB를 빼거나 재부팅하지 않습니다.
6. `UEFI transaction completed and cleaned up`을 확인합니다. 로그의 최종 결과는 `status=0`, `clean=1`이어야 합니다.

## 4. 가장 중요한 Windows 인계

### 수동 실행 (preflight / experimental)

펌웨어 화면으로 돌아오면 다음 경로를 선택합니다.

**F7 → Advanced Mode → Boot → Boot Override → Windows Boot Manager**

이 절차는 해당 ASUS PC 사용자가 실제 성공 후 확인한 경로입니다. 다른 보드·BIOS 버전의 메뉴 위치나 리셋 동작까지 보장하지 않습니다.

**F10이나 Save Changes & Reset을 누르지 않습니다.** 언락한 다음 다시 POST/초기화가 수행되면 적용 상태가 사라질 수 있습니다. 이때는 Windows에서 성공을 가정하지 말고 다시 측정해야 합니다. USB 앱이 다시 열렸다면 Esc로 빠져나오며 같은 부팅에서 U를 반복하지 않습니다.

### Autostart (자동 인계)

`RTX3090-AUTOLOAD.EFI`를 부팅하면 수동 U 확인이 없습니다. 대신:

1. 프로필·PROM·자산 검증을 통과한 후 **5초 카운트다운**이 시작됩니다.
2. 카운트다운 중 **Esc**를 누르면 언락을 건너뛰고 기존 상태로 Windows를 부팅합니다.
3. `U` 키나 다른 키는 카운트다운을 단축하거나 우회할 수 없습니다.
4. 카운트다운이 끝나면 드라이버 릴리스 → 재검증 → 단일 트랜잭션을 수행합니다.
5. GPU 리소스 정리가 증명된 경우에만 Windows Boot Manager를 직접 `StartImage`로 시작합니다.
6. Windows Boot Manager가 반환되면 재시도가 없습니다. 전원을 끄거나 펌웨어 부팅 메뉴를 사용하세요.

카운트다운 시간은 `machine_profile.txt`의 `countdown_seconds`로 설정합니다. `0`으로 설정하면 카운트다운 없이 즉시 실행됩니다.

autostart EFI는 USB 부팅 항목에 수동 실행 EFI와 별도로 등록합니다. 두 EFI는 서로 다른 부팅 경로이므로 혼동하지 않습니다.

## 5. 결과 확인과 실패 대응

Windows 진입 뒤에도 화면은 내장그래픽에 유지하고, [벤치마크](BENCHMARK.ko.md)를 실행합니다. 비교는 동일 실행 파일·라이브러리·전력/클럭 설정·작업 조건으로 합니다. `nvidia-smi`의 드라이버 버전만으로 언락 성공을 판단하지 않습니다.

- `STOP / POWER OFF`, 멈춤, 정리 실패: Windows로 이어서 진행하지 말고 전원을 완전히 끕니다.
- 지원 검사 거부: 강제로 우회하지 말고 `RTX3090-UNLOCK/RUNxx/REPORT.TXT`를 확인합니다.
- UEFI 성공이나 selector readback만으로 Windows 성능 향상을 주장하지 않습니다.
- 복구 시 USB를 제거하고 Windows Boot Manager로 정상 부팅합니다. 기존 Windows EFI는 수정할 필요가 없습니다.
- 다음 부팅에서도 자동으로 유지된다고 가정하지 않습니다. 매번 적용 절차를 거친 뒤 필요하면 다시 측정합니다.

Autostart EFI는 내부 Windows 파티션의 `bootmgfw.efi`를 직접 로드·실행합니다. USB 폴백, BootOrder/BootNext 재작성, ESP 수정을 하지 않습니다. `machine_profile.txt`에 기록된 파티션 GUID·번호·LBA와 정확히 일치하는 단일 파티션만 허용합니다.
