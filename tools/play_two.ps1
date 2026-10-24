# play_two.ps1 - launch two Godot windows for a same-machine playtest.
#
#   powershell -File tools\play_two.ps1
#
# Window A: press Host.  Window B: type 127.0.0.1 in the IP field, press Join.
# Both share the keyboard, so one of them stands still while you play the other;
# that is enough to see hitmarkers, death and respawn from the other side.
# For a real 10-minute duel use two machines and window B joins the host's LAN IP.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'godot_env.ps1')

& (Join-Path $PSScriptRoot 'build_bridge.ps1')
$project = Initialize-BpProject
$godot = Get-BpGodot

Write-Host 'Window A: Host   Window B: Join 127.0.0.1' -ForegroundColor Cyan
Write-Host 'Esc releases the mouse; the in-game menu has Resume / Leave.' -ForegroundColor DarkGray

Start-Process -FilePath $godot -ArgumentList @('--path', $project)
Start-Sleep -Milliseconds 1500
Start-Process -FilePath $godot -ArgumentList @('--path', $project)
Write-Host 'Two instances started. Close both windows when you are done.'