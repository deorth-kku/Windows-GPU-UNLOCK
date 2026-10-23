# RTX 3090 Windows Success Case — 2026-10-20

## Measurement Conditions

- Same RTX 3090 24GiB, NVIDIA Windows 617.14, CUDA Runtime API `12060`, cuBLAS `120600`.
- Same source/executable 4096×4096 GEMM. 3 processes per state; the median of each process's samples is aggregated as the overall median.
- Previous measurement: 2026-10-20 01:25 KST. Current measurement: 2026-10-20 13:30 KST.
- At the start of the current measurement: GPU utilization 0%, VRAM 0MiB, 49°C, no other GPU processes.
- All 3 runs exited with code 0, stderr empty. Post-measurement snapshot: SM 1980MHz, 321.48W, 71°C, software thermal slowdown disabled. The snapshot is not a thermal trace of the entire interval.

| Measurement | Previous median TFLOPS | Current median TFLOPS | Current 3-run range TFLOPS |
|---|---:|---:|---:|
| FP32 | 24.505702 | 24.890281 | 24.726922–24.928998 |
| FP16 Tensor / FP32 accumulate | 70.548081 | 125.907816 | 124.969948–125.909659 |
| BF16 Tensor / FP32 accumulate | 70.529545 | 128.699728 | 128.438012–129.304171

The [machine-readable raw data](../evidence/benchmark.json) provides per-trial metadata, sample statistics, and a summary. It does not include personal paths or device UUIDs. The SHA-256 of the measured executable is also recorded.

## Boot Procedure and Evidence Interpretation

The user reported selecting Save Changes & Reset in BIOS after a previous unlock run. This time, they confirmed entering via **Boot Override → Windows Boot Manager without a reset** after a successful unlock. Performance improvement appeared without a driver replacement.

The successful boot's UEFI log RUN06 records `SS0=88888888`, `SS1=00000008`, `FLR=1`, `FINAL status=0 ... clean=1`. See the [required excerpt](../evidence/uefi-success-excerpt.txt). The original PROM and full USB backup are not published.

Based on this, the interpretation that "a Reset after unlock erased the state, causing the previous failure" is the leading hypothesis. However, without an independently controlled unapplied cold baseline or pre-POST interval measurements, this does not confirm the sole cause, identical behavior on all boards, or general driver compatibility.

Long-term stability, real ComfyUI workflows, persistence after switching to the RTX monitor output, other cards/boards, and internal disk automatic boot were not validated.
