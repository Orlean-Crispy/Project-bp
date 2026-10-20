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
exit 0
