# Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md.
param([string]$VcVars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',[string]$CudaRoot='F:\Program Files\CUDA13')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$nvcc = Join-Path $CudaRoot 'bin'
$runtime = $CudaRoot
$cccl = $CudaRoot
$blas = $CudaRoot
$vcvars = $VcVars
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'Required VS2019 build tools missing' }
$bin = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
# Import vcvars only into this child PowerShell process, never the user/machine environment.
$vcLines = & $env:ComSpec /d /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $vcLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
& (Join-Path $nvcc 'nvcc.exe') -std=c++17 -O3 -arch=sm_86 -Xcompiler /utf-8 `
    "-I$runtime\include" "-I$blas\include" `
    "-L$runtime\lib\x64" "-L$blas\lib\x64" `
    (Join-Path $root 'compute-benchmark-3090.cu') -lcublas -o (Join-Path $bin 'compute-benchmark-3090.exe')
if ($LASTEXITCODE -ne 0) { throw "nvcc failed: $LASTEXITCODE" }
Get-ChildItem -LiteralPath (Join-Path $blas 'bin') -Filter 'cublas*.dll' | Copy-Item -Destination $bin
Get-ChildItem -LiteralPath (Join-Path $blas 'bin') -Filter 'cudart*.dll' | Copy-Item -Destination $bin
$inspect = & (Join-Path $bin 'compute-benchmark-3090.exe') --inspect-only
if ($LASTEXITCODE -ne 0) { throw 'Metadata-only binary check failed' }
$versions = $inspect | ConvertFrom-Json
Write-Output "CUDA runtime: $($versions.cuda_runtime), cuBLAS: $($versions.cublas_major).$($versions.cublas_minor).$($versions.cublas_patch)"
$inspect | Set-Content -LiteralPath (Join-Path $root 'runtime-verification.json') -Encoding utf8
$artifactPaths = @((Join-Path $root 'compute-benchmark-3090.cu'),(Join-Path $root 'upstream-compute-benchmark.cu')) + @(Get-ChildItem -LiteralPath $bin -File | Select-Object -ExpandProperty FullName)
$artifactPaths | ForEach-Object { $f = Get-Item -LiteralPath $_; [pscustomobject]@{Path=$f.FullName.Substring($root.Length+1);Bytes=$f.Length;SHA256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash} } |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $root 'build-manifest.json') -Encoding utf8
Write-Output $inspect
Write-Output 'Build and metadata-only check passed. No GEMM workload was run.'
