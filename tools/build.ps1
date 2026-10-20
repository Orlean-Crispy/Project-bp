# build.ps1 - one command: configure + build. ASCII only on purpose
# (PowerShell 5.1 reads no-BOM files as GBK; Chinese line endings eat the next line).
# Paths here are the stable names written by setup_toolchain.ps1; only that
# script knows which vendor version is installed.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tc = 'C:\bp-toolchain'
$tools = @{
    'g++'   = Join-Path $tc 'mingw64\bin\g++.exe'
    'cmake' = Join-Path $tc 'cmake\bin\cmake.exe'
    'ninja' = Join-Path $tc 'ninja\ninja.exe'
}
foreach ($name in $tools.Keys) {
    if (-not (Test-Path $tools[$name])) {
        Write-Error ("$name not found at " + $tools[$name] + " - run tools/setup_toolchain.ps1 first")
        exit 1
    }
}

$env:PATH = "$tc\mingw64\bin;$tc\cmake\bin;$tc\ninja;$env:PATH"

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
