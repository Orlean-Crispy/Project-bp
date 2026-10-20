# build_bridge.ps1 - builds the GDExtension DLL into godot/bin.
# ASCII only. Paths passed to the linker MUST be relative: GNU ld cannot open
# files whose path contains non-ASCII characters (see docs/pitfalls).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tc = 'C:\bp-toolchain'
$gpp = Join-Path $tc 'mingw64\bin\g++.exe'
$cpp = Join-Path $tc 'godot-cpp'

if (-not (Test-Path $gpp)) { throw "g++ not found at $gpp" }
$lib = Join-Path $cpp 'bin\libgodot-cpp.windows.template_debug.x86_64.a'
if (-not (Test-Path $lib)) { throw 'godot-cpp static library not found' }

# Single source of truth shared with the "sim" target in CMakeLists.txt.
$listed = Get-Content (Join-Path $PSScriptRoot 'sim_sources.txt') |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -ne '' -and -not $_.StartsWith('#') }
$sources = @('bridge/sim_bridge.cpp', 'bridge/mesh_builder.cpp', 'bridge/net_gateway.cpp') + $listed

New-Item -ItemType Directory -Force -Path (Join-Path $root 'godot\bin') | Out-Null
$out = 'godot/bin/breakpoint_bridge.windows.template_debug.x86_64.dll'

Push-Location $root
try {
    $opt = @(
        '-shared', '-std=c++17', '-O2', '-ffp-contract=off', '-DWINDOWS_ENABLED', '-DTEMPLATE_ENABLED',
        '-DBP_PROTOCOL_VERSION=3', '-DBP_SIM_VERSION=1',
        '-I.', "-I$cpp\include", "-I$cpp\gen\include", "-I$cpp\gdextension"
    ) + $sources + @(
        '-Wl,--whole-archive', $lib, '-Wl,--no-whole-archive',
        '-static-libgcc', '-static-libstdc++',
        '-Wl,-Bstatic', '-lwinpthread', '-Wl,-Bdynamic',
        '-lws2_32',
        '-o', $out
    )
    & $gpp $opt
    if ($LASTEXITCODE -ne 0) { throw 'bridge build failed' }
} finally {
    Pop-Location
}
Write-Host "built $out"