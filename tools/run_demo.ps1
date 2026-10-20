# run_demo.ps1 - build the bridge, then launch the Godot demo.
# The Godot path and the first-run import live in tools/godot_env.ps1 (AUD-15),
# so they are written once instead of in every launcher.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'godot_env.ps1')

& (Join-Path $PSScriptRoot 'build_bridge.ps1')

$project = Initialize-BpProject
& (Get-BpGodot) --path $project
