# 검증한 하드웨어와 고정 검사

이 공개본은 모델명만 확인하는 도구가 아닙니다. 아래는 실행 코드가 검증하는 주요 조건입니다.

| 구분 | 값 |
|---|---|
| GPU PCI | vendor `10de`, device `2204` |
| GPU subsystem | vendor `10de`, device `1454` |
| GPU BDF | `0000:01:00.0` |
| Chip | `0xb72000a1` |
| VRAM | `0x6000` MiB = 24GiB |
| BAR0 | `0xf5000000`, 16MiB |
| PROM SHA-256 | `8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4` |
| iGPU PCI | `1002:164e`, subsystem `1043:8877` |
| iGPU BDF/GOP | `0000:0c:00.0` 및 캡처된 AMD 출력 경로 |
| Secure Boot | 꺼짐 |
| 성공 프로필 | `entryLatchedSs1Full` |

실제 PC: ASUS TUF GAMING B650M-PLUS WIFI / Ryzen 7 7800X3D / Windows 11 Home 26200 / NVIDIA 617.14.

`unlock/port.h`, `port.c`, `checks.c`, `app.c`가 현재 검사의 근거입니다. 정확한 기존 PCI 상태·PROM·출력 경로 등 추가 검사가 있습니다. 이 표는 지원을 확대하는 허용 목록이 아닙니다.

다른 3090이나 같은 모델의 다른 PC도 지원이 확인되지 않았습니다. 다른 카드의 PROM 해시를 입력하거나 검사 조건을 삭제하는 것만으로 안전한 포팅이 되지 않습니다. 별도 포팅에는 메모리/WPR 배치·펌웨어 호환성·PCI 소유권·DMA 정리·오류 시 차단을 검증할 수 있는 개발 지식이 필요합니다.

## Autostart 추가 요구 사항

Autostart 빌드(`RTX3090-AUTOLOAD.EFI`)는 위 조건 외에도 다음을 요구합니다:

| 항목 | 요구 사항 |
|---|---|
| `machine_profile.txt` | 로컬 Windows 파티션의 GUID·번호·시작 LBA·크기가 정확히 기록되어야 함 |
| 파티션 식별 | UEFI가 볼 수 있는 SimpleFileSystem + DevicePath 중 정확히 1개만 프로필과 일치해야 함 |
| USB 제외 | USB DevicePath 노드가 포함된 후보는 자동 거부 |
| Windows 로더 | `\EFI\Microsoft\Boot\bootmgfw.efi`가 해당 파티션에 존재해야 함 |
| 프로필 | `entryLatchedSs1Preserve`만 autostart와 함께 사용 가능 |

`machine_profile.txt`는 `tools/prepare_usb.py --autostart`로 자동 생성되거나 수동으로 작성할 수 있습니다. 파일이 없거나 키가 누락되면 빌드가 실패합니다.
