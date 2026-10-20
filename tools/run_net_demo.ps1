# run_net_demo.ps1 - launch a local two-instance LAN demo.
# Host signals on 27115 (UDP 27116); the client connects to 127.0.0.1.
# ASCII only. Close both windows when done.
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

$port = 27115
$host = Start-Process -FilePath $godot -ArgumentList @(
    '--path', $project, '--', '--host', "--port=$port"
) -PassThru
Write-Host "host started (pid $($host.Id)) on port $port"
Start-Sleep -Milliseconds 1500

Write-Host 'starting client'
& $godot --path $project -- --connect=127.0.0.1 "--port=$port"

Write-Host 'client closed; host is still running (close its window to stop).'
