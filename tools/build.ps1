# build.ps1 - one command: configure + build. ASCII only on purpose
# (PowerShell 5.1 reads no-BOM files as GBK; Chinese line endings eat the next line).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tc = 'C:\bp-toolchain'
if (-not (Test-Path (Join-Path $tc 'mingw64\bin\g++.exe'))) {
    Write-Error "toolchain not found at $tc - run tools/setup_toolchain.ps1 first"
    exit 1
}

$env:PATH = "$tc\mingw64\bin;$tc\cmake-4.4.4-windows-x86_64\bin;$tc\ninja;$env:PATH"

Push-Location $root
try {
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build build
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
