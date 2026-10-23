# Performance Comparison Under Identical Conditions

The measurement source uses the upstream project's 4096×4096 cuBLAS GEMM. FP32 pedantic, FP16/BF16 input with FP32 accumulation and output, 2-second warmup and 30 samples per precision. GPU selection was changed to 3090 and a metadata verification option was added. Result verification is limited to first/last element checks on the output, same as the original.

CUDA 12.6 redist is prepared in the project folder only, with hash and size verified from the official NVIDIA server. Driver, ComfyUI, and system PATH are not modified. Download size and disk space are required.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\run-benchmark.ps1 -PreflightOnly
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\benchmark\run-benchmark.ps1
```

The measured build used VS2019 BuildTools MSVC14.29. Specify a different location with `build.ps1 -VcVars <vcvars64.bat>`. Use an MSVC compatible with CUDA 12.6. Setup does not overwrite an existing packages folder, so run it once from a new directory.

Save and close ComfyUI, video, recording, and other GPU workloads before measuring. Starting conditions: GPU utilization ≤ 5%, VRAM ≤ 2048MiB, temperature ≤ 65°C. The script saves 3 runs to a new `benchmark/results/<timestamp>/` folder. It does not force-kill other processes.

Use the same executable, driver, power limits, overclocking, and temperature conditions before and after the unlock. This tool does not change GPU settings. FP16/BF16 Tensor improvement does not imply a proportional improvement in total game, video, or ComfyUI time.
