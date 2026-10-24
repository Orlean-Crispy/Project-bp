# godot_check.ps1 - proves the GDExtension really loads and the HUD composes.
#
# This is the check that would have caught the extension silently failing to
# load: a release editor build only accepts the "editor" key in
# breakpoint.gdextension, and with only "windows.debug" Godot does not load the
# DLL at all. main.gd then fails to parse ("SimWorld not declared") and every
# later demo check is meaningless - which is exactly what happened on 2026-10-20.
#
#   godot_check.ps1                 hud check + two-process reconnect check
#   godot_check.ps1 -SkipReconnect  hud check only (fast)
#
# Exit codes: 0 pass, 1 fail, 3 skipped (no Godot or no bridge DLL).
param(
    [switch]$SkipReconnect
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'godot_env.ps1')

function Fail([string]$message) {
    Write-Host ('FAIL: ' + $message) -ForegroundColor Red
    exit 1
}

$dll = Join-Path $root 'godot\bin\breakpoint_bridge.windows.template_debug.x86_64.dll'
if (-not (Test-Path $dll)) {
    Write-Host 'SKIP: bridge DLL not built (run tools/build_bridge.ps1)'
    exit 3
}
try {
    $null = Get-BpGodot
} catch {
    Write-Host ('SKIP: ' + $_.Exception.Message)
    exit 3
}

# Ports come from sim/net/ports.h; a literal in this file would fail
# tools/check_sources.ps1, and parsing the header also proves the convention is
# still readable.
$header = Get-Content (Join-Path $root 'sim\net\ports.h') -Raw
$basePort = [regex]::Match($header, 'kDefaultSignalPort\s*=\s*(\d+)')
$gateOffset = [regex]::Match($header, 'kGatePortOffset\s*=\s*(\d+)')
if (-not $basePort.Success -or -not $gateOffset.Success) {
    Fail 'sim/net/ports.h has no kDefaultSignalPort / kGatePortOffset'
}
$defaultPort = [int]$basePort.Groups[1].Value
$gatePort = $defaultPort + [int]$gateOffset.Groups[1].Value

Write-Host ('== hud check (default port ' + $defaultPort + ') ==')
$hud = Invoke-BpGodotHeadless -Arguments @('--hud-check')
$lines = $hud.Output.Split([char]10) | ForEach-Object { $_.TrimEnd([char]13) }
$hudLines = @($lines | Where-Object { $_ -like 'hud hp=*' })
if ($hudLines.Count -ne 1) {
    Write-Host $hud.Output
    Write-Host $hud.Error
    Fail 'no hud line: the extension did not load, or the HUD never composed'
}
$hudLine = $hudLines[0]
$vertices = [regex]::Match($hudLine, 'vertices=(\d+)')
$portField = [regex]::Match($hudLine, 'port=(\d+)')
if (-not $vertices.Success -or [int]$vertices.Groups[1].Value -le 0) {
    Fail ('empty mesh: ' + $hudLine)
}
if (-not $portField.Success -or [int]$portField.Groups[1].Value -ne $defaultPort) {
    Fail ('wrong default port: ' + $hudLine)
}
Write-Host $hudLine
Write-Host 'PASS: extension loaded, HUD composed, default port wired' -ForegroundColor Green

# Render interpolation: the camera blends the two newest ticks, which only works
# if the bridge really keeps a previous tick. Equal prev/cur while the player is
# moving means the history is missing and the view is back to stepping at 64 Hz.
Write-Host '== render interpolation =='
$interp = Invoke-BpGodotHeadless -Arguments @('--autoplay', '--interp-check')
$interpLines = @($interp.Output.Split([char]10) | ForEach-Object { $_.TrimEnd([char]13) } |
    Where-Object { $_ -like 'interp prev=*' })
if ($interpLines.Count -ne 1) {
    Write-Host $interp.Output
    Write-Host $interp.Error
    Fail 'no interp line: the bridge does not expose the previous tick'
}
$interpLine = $interpLines[0]
$gapField = [regex]::Match($interpLine, 'gap=([0-9.]+)')
if (-not $gapField.Success -or [double]$gapField.Groups[1].Value -le 0.01) {
    Fail ('the previous tick has no movement in it: ' + $interpLine)
}
Write-Host $interpLine
Write-Host 'PASS: previous tick exposed, camera can interpolate' -ForegroundColor Green

# M6.11: a tracer has to come out of the whole chain (trigger -> shot -> event ->
# GDScript). Solo holds the trigger for ~1 s and counts the events it drained.
Write-Host '== tracers =='
$fire = Invoke-BpGodotHeadless -Arguments @('--fire-check')
$fireLines = @($fire.Output.Split([char]10) | ForEach-Object { $_.TrimEnd([char]13) } |
    Where-Object { $_ -like 'tracer events=*' })
if ($fireLines.Count -ne 1) {
    Write-Host $fire.Output
    Fail 'no tracer line: solos shots never produced an event'
}
$tracerCount = [int]([regex]::Match($fireLines[0], 'events=(\d+)').Groups[1].Value)
if ($tracerCount -le 0) { Fail ('no tracer events: ' + $fireLines[0]) }
Write-Host $fireLines[0]
Write-Host 'PASS: the trigger produced tracer events end to end' -ForegroundColor Green

if ($SkipReconnect) { exit 0 }

Write-Host '== reconnect check (two processes) =='
# Uses the gate port so it can run next to a developer's host.
$project = Initialize-BpProject
$godot = Get-BpGodot
$temp = [System.IO.Path]::GetTempFileName()
$hostLog = $temp + '.host'
$clientLog = $temp + '.client'
$hostProcess = $null
$clientProcess = $null
try {
    $hostProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $hostLog -RedirectStandardError ($hostLog + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--host', ('--port=' + $gatePort))
    Start-Sleep -Milliseconds 2500
    $clientProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $clientLog -RedirectStandardError ($clientLog + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--connect=127.0.0.1', ('--port=' + $gatePort), '--reconnect-check')
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path $clientLog) {
            if (Select-String -Path $clientLog -Pattern 'reconnect double=' -Quiet) { break }
        }
    }
} finally {
    foreach ($process in @($clientProcess, $hostProcess)) {
        if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
    Start-Sleep -Milliseconds 300
}
$clientText = if (Test-Path $clientLog) { Get-Content -LiteralPath $clientLog -Raw } else { '' }
$hostText = if (Test-Path $hostLog) { Get-Content -LiteralPath $hostLog -Raw } else { '' }
Remove-Item -LiteralPath $hostLog, ($hostLog + '.err'), $clientLog, ($clientLog + '.err') -Force -ErrorAction SilentlyContinue

if ($clientText -notmatch 'joined slot 1') { Fail 'client never joined the host' }
if ($clientText -notmatch 'reconnect ok') { Fail 'reconnect failed' }
if ($clientText -notmatch 'reconnect double=0') { Fail 'a second reconnect was not refused (AUD-17)' }
if ($hostText -notmatch 'signal slot 1 resumed 1') { Fail 'host did not resume the ticket' }
Write-Host 'PASS: reconnect resumed the ticket and the second call was refused' -ForegroundColor Green

# AUD-02: with the pause menu open the world must keep pumping the network. The
# server drops a silent link after 5 s, so a 7 s paused run that still shows
# "peers 1" and no SERVER SILENT is the gate. --force-paused used to be read but
# never parsed, so this behaviour was never actually checked.
Write-Host '== paused menu keeps the link =='
$pausedLimit = 7
$hostLog2 = $temp + '.host2'
$clientLog2 = $temp + '.client2'
$hostProcess = $null
$clientProcess = $null
try {
    $hostProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $hostLog2 -RedirectStandardError ($hostLog2 + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--host', ('--port=' + $gatePort))
    Start-Sleep -Milliseconds 2500
    $clientProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $clientLog2 -RedirectStandardError ($clientLog2 + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--connect=127.0.0.1', ('--port=' + $gatePort), '--force-paused')
    Start-Sleep -Seconds $pausedLimit
} finally {
    foreach ($proc in @($clientProcess, $hostProcess)) {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
    Start-Sleep -Milliseconds 300
}
$pausedText = if (Test-Path $clientLog2) { Get-Content -LiteralPath $clientLog2 -Raw } else { '' }
Remove-Item -LiteralPath $hostLog2, ($hostLog2 + '.err'), $clientLog2, ($clientLog2 + '.err') -Force -ErrorAction SilentlyContinue
if ($pausedText -notmatch 'joined slot 1') { Fail 'the paused client never joined' }
if ($pausedText -notmatch 'peers 1') { Fail 'the paused client never saw the host' }
if ($pausedText -match 'server silent') { Fail 'the host dropped a paused client (AUD-02 regression)' }
Write-Host 'PASS: the link survived 7 s with the menu open' -ForegroundColor Green

# M6.8: damage must cross the snapshot into the other side's HUD. The host hurts
# slot 1 through the real apply_damage path; the client only prints its HUD, so
# an hp=70 line there means the snapshot carried it (AUD-16's other half).
Write-Host '== damage reaches the other HUD =='
$damageHost = $temp + '.dhost'
$damageClient = $temp + '.dclient'
$hostProcess = $null
$clientProcess = $null
try {
    $hostProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $damageHost -RedirectStandardError ($damageHost + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--host', ('--port=' + $gatePort), '--damage-check')
    Start-Sleep -Milliseconds 2500
    $clientProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $damageClient -RedirectStandardError ($damageClient + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--connect=127.0.0.1', ('--port=' + $gatePort), '--hud-damage-check')
    # Hurt -> kill -> watch: the host drives the stages, so wait for its last
    # line and then give the client the 5 s respawn plus slack.
    $damageDeadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $damageDeadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path $damageHost) {
            if (Select-String -Path $damageHost -Pattern 'watching slot 1 respawn' -Quiet) { break }
        }
    }
    Start-Sleep -Seconds 7
} finally {
    foreach ($proc in @($clientProcess, $hostProcess)) {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
    Start-Sleep -Milliseconds 300
}
$damageHostText = if (Test-Path $damageHost) { Get-Content -LiteralPath $damageHost -Raw } else { '' }
$damageClientText = if (Test-Path $damageClient) { Get-Content -LiteralPath $damageClient -Raw } else { '' }
Remove-Item -LiteralPath $damageHost, ($damageHost + '.err'), $damageClient, ($damageClient + '.err') -Force -ErrorAction SilentlyContinue
if ($damageHostText -notmatch 'damage slot 1 hp=70') { Fail 'the host never applied the damage' }
if ($damageHostText -notmatch 'killed slot 1 hp=0') { Fail 'the host never killed slot 1' }
# Order matters: hurt (70) -> dead (0) -> back (100). A plain -match would pass
# even if the client printed them in the wrong order or never died at all.
$hurtAt = $damageClientText.IndexOf('hud hp=70')
$deadAt = $damageClientText.IndexOf('hud hp=0')
$backAt = $damageClientText.LastIndexOf('hud hp=100')
if ($hurtAt -lt 0) { Fail 'the client HUD never showed the damage (snapshot -> HUD broken)' }
if ($deadAt -lt 0) { Fail 'the client HUD never showed the death' }
if ($backAt -le $deadAt) { Fail 'the client never came back from a respawn' }
Write-Host ('death seq: hurt@{0} dead@{1} back@{2}' -f $hurtAt, $deadAt, $backAt)
Write-Host 'PASS: damage, death and respawn crossed the snapshot into the other HUD' -ForegroundColor Green

# M6.11: the hitmarker has to be a confirmed hit from the snapshot, not a guess.
# The host aims at the joining player and fires for real (no test hook), then
# reports the hit record it read back. A miss would print nothing and time out.
Write-Host '== hitmarker =='
$hitHost = $temp + '.hhost'
$hitClient = $temp + '.hclient'
$hostProcess = $null
$clientProcess = $null
try {
    $hostProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $hitHost -RedirectStandardError ($hitHost + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--host', ('--port=' + $gatePort), '--hit-check')
    Start-Sleep -Milliseconds 2500
    $clientProcess = Start-Process -FilePath $godot -PassThru -NoNewWindow -RedirectStandardOutput $hitClient -RedirectStandardError ($hitClient + '.err') -ArgumentList @('--headless', '--path', $project, '--', '--connect=127.0.0.1', ('--port=' + $gatePort))
    $hitDeadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $hitDeadline) {
        Start-Sleep -Milliseconds 250
        if (Test-Path $hitHost) {
            if (Select-String -Path $hitHost -Pattern 'hitmark serial=' -Quiet) { break }
        }
    }
} finally {
    foreach ($proc in @($clientProcess, $hostProcess)) {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    }
    Start-Sleep -Milliseconds 300
}
$hitText = if (Test-Path $hitHost) { Get-Content -LiteralPath $hitHost -Raw } else { '' }
Remove-Item -LiteralPath $hitHost, ($hitHost + '.err'), $hitClient, ($hitClient + '.err') -Force -ErrorAction SilentlyContinue
if ($hitText -notmatch 'hitmark serial=') { Fail 'the host never confirmed a hit (no hitmarker)' }
$hitLine = [regex]::Match($hitText, 'hitmark serial=.*').Value
if ($hitLine -notmatch 'amount=([1-9]\d*)') { Fail ('a hitmarker with no damage: ' + $hitLine) }
Write-Host $hitLine
Write-Host 'PASS: a real shot produced a confirmed hitmarker' -ForegroundColor Green

exit 0
