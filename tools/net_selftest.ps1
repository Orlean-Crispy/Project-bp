# net_selftest.ps1 - two-process LAN check (not part of smoke: it uses wall time).
# Starts one server and two clients over real UDP, then verifies joins, input
# application and snapshot flow from the logs.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $exe = Join-Path (Get-Location) 'build\net_loop.exe'
    if (-not (Test-Path $exe)) { throw 'net_loop.exe not built; run tools/build.ps1' }

    $port = 27130
    $serverLog = 'build\net_selftest_server.log'
    $c0Log = 'build\net_selftest_c0.log'
    $c1Log = 'build\net_selftest_c1.log'

    $server = Start-Process -FilePath $exe -ArgumentList @(
        '--serve', '--port', "$port", '--players', '2', '--seconds', '5'
    ) -PassThru -NoNewWindow -RedirectStandardOutput $serverLog -RedirectStandardError 'build\net_selftest_server.err'

    Start-Sleep -Milliseconds 600
    $c0 = Start-Process -FilePath $exe -ArgumentList @(
        '--join', '127.0.0.1', '--port', "$port", '--slot', '0', '--seconds', '4'
    ) -PassThru -NoNewWindow -RedirectStandardOutput $c0Log -RedirectStandardError 'build\net_selftest_c0.err'
    $c1 = Start-Process -FilePath $exe -ArgumentList @(
        '--join', '127.0.0.1', '--port', "$port", '--slot', '1', '--seconds', '4'
    ) -PassThru -NoNewWindow -RedirectStandardOutput $c1Log -RedirectStandardError 'build\net_selftest_c1.err'

    $c0.WaitForExit()
    $c1.WaitForExit()
    $server.WaitForExit()

    $serverText = Get-Content $serverLog -Raw
    $c0Text = Get-Content $c0Log -Raw
    $c1Text = Get-Content $c1Log -Raw

    $joins = ([regex]::Matches($serverText, 'join slot=')).Count
    $all = [regex]::Matches($serverText, 'acc=(\d+)\s+late=(\d+)\s+far=(\d+)')
    $stats = if ($all.Count -gt 0) { $all[$all.Count - 1] } else { $null }
    $accepted = if ($stats.Success) { [int]$stats.Groups[1].Value } else { 0 }
    $far = if ($stats.Success) { [int]$stats.Groups[3].Value } else { 999 }
    $final = [regex]::Match($serverText, 'final slot=0 pos=(-?[\d.]+),(-?[\d.]+),')
    $moved = if ($final.Success) {
        [math]::Abs([double]$final.Groups[1].Value + 16.0) + [math]::Abs([double]$final.Groups[2].Value)
    } else { 0.0 }

    $clientOk = $true
    foreach ($text in @($c0Text, $c1Text)) {
        $m = [regex]::Match($text, 'final joined=1 snapshots=(\d+)')
        if (-not $m.Success -or [int]$m.Groups[1].Value -lt 100) { $clientOk = $false }
    }

    Write-Host "joins=$joins accepted=$accepted far=$far moved=$([math]::Round($moved, 2)) clients_ok=$clientOk"
    if ($joins -ne 2 -or $accepted -lt 100 -or $far -ne 0 -or $moved -lt 2.0 -or -not $clientOk) {
        Write-Host 'FAIL: two-process LAN check failed' -ForegroundColor Red
        exit 1
    }
    Write-Host 'PASS: two processes joined, inputs applied and snapshots flowed' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
