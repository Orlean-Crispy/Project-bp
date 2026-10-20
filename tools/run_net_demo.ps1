# run_net_demo.ps1 - launch a local two-instance LAN demo.
# Both instances use the default signal port from sim/net/ports.h (the game port
# is that + 1); the client connects to 127.0.0.1. ASCII only.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$godot = 'C:\bp-toolchain\godot\Godot_v4.5-stable_win64.exe'
if (-not (Test-Path $godot)) { throw "Godot not found at $godot" }

& (Join-Path $PSScriptRoot 'build_bridge.ps1')

$project = Join-Path $root 'godot'
if (-not (Test-Path (Join-Path $project '.godot'))) {
    Write-Host 'first run: importing project'
    & $godot --headless --editor --path $project --quit
}

$server = Start-Process -FilePath $godot -ArgumentList @(
    '--path', $project, '--', '--host'
) -PassThru
Write-Host "host started (pid $($server.Id))"
Start-Sleep -Milliseconds 1500

Write-Host 'starting client'
& $godot --path $project -- --connect=127.0.0.1

Write-Host 'client closed; host is still running (close its window to stop).'
