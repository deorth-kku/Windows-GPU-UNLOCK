# From USB Boot to Windows Measurement

## 1. Scope and Preparation

First verify that your system matches the [supported conditions](COMPATIBILITY.en.md). The execution checks in this public release are pinned to the actually validated PC. Supporting a different system requires a separate port including PCI/MMIO/DMA, GOP, PROM, and memory layout validation.

The UEFI app modifies GPU state, so save your work in preparation for errors during execution. For PCs using BitLocker/device encryption, verify your recovery key before changing Secure Boot settings. The EFI and recovery partitions on the current Windows disk are not modified.

- Connect the display cable to the motherboard's integrated graphics output.
- **Keep the RTX 3090's auxiliary power cables and PCIe slot connection intact.** "Unplug the cable" refers to the RTX's monitor output connection.
- Verify that BIOS and Windows display correctly on the integrated graphics.
- Confirm x64 UEFI boot and Secure Boot disabled. Complete any setting changes/saves before running the unlock.
- Prepare a FAT32 USB with sufficient space and back up existing files separately. This project's packaging tool does not format USBs or shrink disks.

## 2. Executable and Asset Preparation

From the repository root, place firmware in `assets/` and run:

```powershell
python .\tools\prepare_usb.py --assets .\assets --output .\usb-package
```

If hash and size verification pass, a new local folder is created. Copy the **contents** of `usb-package` to the top level of a FAT32 USB. If you need to overwrite existing destination files, back them up first and verify the contents and destination.

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

Copying only `BOOTX64.EFI` will not work. The `RTX3090-UNLOCK` folder must be writable for logs.

For autostart packaging, add `--autostart` to collect local partition info and generate `machine_profile.txt`:

```powershell
python .\tools\prepare_usb.py --assets .\assets --output .\usb-package --autostart
```

## 3. Running the Unlock

1. Save work in ComfyUI or other applications. Shut down Windows completely (hold Shift while shutting down), then power on. Checks may be rejected in a warm state where the driver has already been used.
2. At the ASUS logo, press F8 and select **UEFI USB**.
3. Verify profile `entryLatchedSs1Full` and preflight check pass.
4. **Press U once.** Esc or a 30-second wait without confirmation cancels.
5. Do not remove the USB or reboot during execution.
6. Confirm `UEFI transaction completed and cleaned up`. The final log result should be `status=0`, `clean=1`.

## 4. The Critical Windows Handoff

### Manual execution (preflight / experimental)

After returning to the firmware screen, select:

**F7 → Advanced Mode → Boot → Boot Override → Windows Boot Manager**

This path was confirmed by the ASUS PC user after actual success. It does not guarantee menu locations or reset behavior on other boards or BIOS versions.

**Do not press F10 or Save Changes & Reset.** If POST/initialization runs again after the unlock, the applied state may be lost. In that case, do not assume success in Windows; re-measure. If the USB app reopens, press Esc to exit and do not repeat U in the same boot.

### Autostart (automatic handoff)

Booting `RTX3090-AUTOLOAD.EFI` requires no manual U confirmation. Instead:

1. After profile, PROM, and asset validation pass, a **5-second countdown** begins.
2. Pressing **Esc** during the countdown skips the unlock and boots Windows with the existing state.
3. The `U` key or any other key cannot shorten or bypass the countdown.
4. When the countdown ends, the driver is released, revalidated, and a single transaction is performed.
5. Only when GPU resource cleanup is proven is the Windows Boot Manager started directly via `StartImage`.
6. If the Windows Boot Manager returns, there is no retry. Power off or use the firmware boot menu.

The countdown duration is configured by `countdown_seconds` in `machine_profile.txt`; set it to `0` to arm immediately with no countdown.

Register the autostart EFI as a separate USB boot entry from the manual execution EFI. They are distinct boot paths; do not confuse them.

## 5. Result Verification and Failure Handling

After entering Windows, keep the display on the integrated graphics and run the [benchmark](BENCHMARK.en.md). Compare using the same executable, libraries, power/clock settings, and workload conditions. Do not judge unlock success by the `nvidia-smi` driver version alone.

- `STOP / POWER OFF`, hang, cleanup failure: do not continue to Windows; power off completely.
- Support check rejection: do not force a bypass; check `RTX3090-UNLOCK/RUNxx/REPORT.TXT`.
- Do not claim Windows performance improvement based on UEFI success or selector readback alone.
- To recover, remove the USB and boot normally via Windows Boot Manager. The existing Windows EFI does not need modification.
- Do not assume the state persists across reboots. Go through the application procedure each time and re-measure if needed.

The autostart EFI loads and executes `bootmgfw.efi` directly from the internal Windows partition. It does not perform USB fallback, BootOrder/BootNext rewriting, or ESP modification. Only a single partition exactly matching the GUID, number, and LBA recorded in `machine_profile.txt` is accepted.
