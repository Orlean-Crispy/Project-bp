# run_demo.ps1 - build the bridge if needed, then launch the Godot demo.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$godot = 'C:\bp-toolchain\godot\Godot_v4.5-stable_win64.exe'
if (-not (Test-Path $godot)) { throw "Godot not found at $godot - run tools/setup_toolchain.ps1" }

& (Join-Path $PSScriptRoot 'build_bridge.ps1')

$project = Join-Path $root 'godot'
if (-not (Test-Path (Join-Path $project '.godot'))) {
    Write-Host 'first run: importing project'
    & $godot --headless --editor --path $project --quit
}
& $godot --path $project