# Validated Hardware and Fixed Checks

This public release is not a model-name-only checker. The following are the key conditions that the execution code validates.

| Item | Value |
|---|---|
| GPU PCI | vendor `10de`, device `2204` |
| GPU subsystem | vendor `10de`, device `1454` |
| GPU BDF | `0000:01:00.0` |
| Chip | `0xb72000a1` |
| VRAM | `0x6000` MiB = 24GiB |
| BAR0 | `0xf5000000`, 16MiB |
| PROM SHA-256 | `8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4` |
| iGPU PCI | `1002:164e`, subsystem `1043:8877` |
| iGPU BDF/GOP | `0000:0c:00.0` and the captured AMD output path |
| Secure Boot | Disabled |
| Success profile | `entryLatchedSs1Full` |

Actual PC: ASUS TUF GAMING B650M-PLUS WIFI / Ryzen 7 7800X3D / Windows 11 Home 26200 / NVIDIA 617.14.

`unlock/port.h`, `port.c`, `checks.c`, and `app.c` are the basis for the current checks. Additional checks exist for exact prior PCI state, PROM, and output paths. This table is not an allowlist for expanding support.

Support for other 3090s or the same model on different PCs has not been confirmed. Simply entering a different card's PROM hash or removing check conditions does not constitute a safe port. A separate port requires development knowledge to validate memory/WPR layout, firmware compatibility, PCI ownership, DMA cleanup, and error-time blocking.

## Autostart Additional Requirements

The autostart build (`RTX3090-AUTOLOAD.EFI`) requires the following in addition to the conditions above:

| Item | Requirement |
|---|---|
| `machine_profile.txt` | Must accurately record the local Windows partition's GUID, number, start LBA, and size |
| Partition identity | Exactly one SimpleFileSystem + DevicePath candidate visible to UEFI must match the profile |
| USB exclusion | Candidates containing USB DevicePath nodes are automatically rejected |
| Windows loader | `\EFI\Microsoft\Boot\bootmgfw.efi` must exist on the corresponding partition |
| Profile | Only `entryLatchedSs1Preserve` may be used with autostart |

`machine_profile.txt` can be generated automatically by `tools/prepare_usb.py --autostart` or written manually. If the file is missing or a key is absent, the build fails.
