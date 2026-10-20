# build_bridge.ps1 - builds the GDExtension DLL into godot/bin.
# ASCII only. Paths passed to the linker MUST be relative: GNU ld cannot open
# files whose path contains non-ASCII characters (see docs/pitfalls).
#
# AUD-13: the flags match the CMake target. The bridge compiles the same sim
# sources, so a flag that only exists on one side (or a missing build id) lets
# the DLL and headless_sim disagree on movement and hashes without any test
# noticing. godot-cpp headers are included with -isystem so their warnings can
# never fail our build; -Werror applies to our code.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tc = 'C:\bp-toolchain'
$gpp = Join-Path $tc 'mingw64\bin\g++.exe'
$cpp = Join-Path $tc 'godot-cpp'

if (-not (Test-Path $gpp)) { throw "g++ not found at $gpp" }
$lib = Join-Path $cpp 'bin\libgodot-cpp.windows.template_debug.x86_64.a'
if (-not (Test-Path $lib)) { throw 'godot-cpp static library not found' }

# Build id: the same git hash CMake bakes into headless_sim, so a recorded demo
# or a log line says whether the DLL and the exe came from the same commit.
$buildId = 'nogit'
$hash = & git -C $root rev-parse --short HEAD 2>$null
if ($LASTEXITCODE -eq 0 -and $hash) { $buildId = ($hash | Select-Object -First 1).Trim() }
# Backslash-escaped quotes are the only form that survives PowerShell's native
# argument passing: plain "..." reaches g++ as a bare identifier.
$defBuildId = '-DBP_BUILD_ID=\"' + $buildId + '\"'

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
        '-shared', '-std=c++17', '-O2', '-DNDEBUG', '-ffp-contract=off',
        '-Wall', '-Wextra', '-Werror',
        '-DWINDOWS_ENABLED', '-DTEMPLATE_ENABLED', $defBuildId,
        '-I.', '-isystem', (Join-Path $cpp 'include'), '-isystem', (Join-Path $cpp 'gen\include'),
        '-isystem', (Join-Path $cpp 'gdextension')
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
Write-Host ("built $out (build id $buildId)")
