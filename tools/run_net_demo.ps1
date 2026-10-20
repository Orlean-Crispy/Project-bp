# run_net_demo.ps1 - launch a local two-instance LAN demo.
# Both instances use the default signal port from sim/net/ports.h (the game port
# is that + 1); the client connects to 127.0.0.1. ASCII only.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'godot_env.ps1')

& (Join-Path $PSScriptRoot 'build_bridge.ps1')

$project = Initialize-BpProject
$godot = Get-BpGodot
$hostProcess = Start-Process -FilePath $godot -ArgumentList @('--path', $project, '--', '--host') -PassThru
Write-Host ('host started (pid ' + $hostProcess.Id + ')')
Start-Sleep -Milliseconds 1500

Write-Host 'starting client'
& $godot --path $project -- --connect=127.0.0.1

Write-Host 'client closed; host is still running (close its window to stop).'
