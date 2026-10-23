# Execution Firmware Preparation

This public release does not include NVIDIA firmware, VBIOS/PROM dumps, or driver DLLs. The exact sizes and SHA-256 hashes of the required files are in [unlock/assets.json](../unlock/assets.json).

Follow the upstream core's [pinned commit firmware guide](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956/firmware) to extract and generate from a legally obtainable original. The firmware lineage for this case is **GA102 / 610.43.03**. This is not a directive for the Windows driver version to install. The actual Windows driver that succeeded was 617.14.

## Generating with the Upstream Tool

Prepare Git, make, tar, and **Python 3.14 or later** in WSL/Linux. Python 3.14 is the version required by the upstream tool for `.tar.zst` input handling. This is different from the Python requirement for the Windows UEFI build/packaging.

Run from a new working directory. The commands below download the source and generate assets; they do not install or load Linux GPU modules. `sudo`, `make install-*`, and DKMS installation are not needed for this Windows procedure.

```sh
git clone https://github.com/Zagitta/gpu-unlock.git gpu-unlock-firmware-source
cd gpu-unlock-firmware-source
git checkout --detach 0ebdb1c6308b6a2072be5bf04513472942541956
make inputs firmware PYTHON=python3.14 INPUTS="$PWD/inputs" ASSETS="$PWD/assets"
```

The upstream `fetch_artifacts.py` verifies pinned NVIDIA sources and nvidia-utils inputs by SHA-256, and the generation scripts check output hashes. Copy the following 5 `.bin` files from the generated `assets/` to the Windows GPU UNLOCK local `assets/`. Also review and keep the upstream-generated `NVIDIA-LICENSE` and [firmware/NOTICE.md](https://github.com/Zagitta/gpu-unlock/blob/0ebdb1c6308b6a2072be5bf04513472942541956/firmware/NOTICE.md). The generation process was not re-run in this public preparation session; the existing validated assets and packaging tool confirmed size and hash match for all 5 files.

Prepare the following 5 files in the local `assets/` directory:

```text
booter_load_ga102.bin
v67-ga102-610.43.03.bin
gsp_ga10x.bin
gsp_boot_ga102.bin
gsp_boot_desc_ga102.bin
```

Do not substitute files with similar versions. The packaging tool and UEFI app reject files with different hashes. If you cannot verify the original source, generation method, and license, the execution preparation is not complete.
