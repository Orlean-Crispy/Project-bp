# net_soak.ps1 - the automated half of E9: two clients stay connected for N seconds.
# A human still has to judge the feel; this proves the link does not rot, the
# server does not run out of slots, and the client keeps receiving snapshots.
#
#   net_soak.ps1            # 600 s (10 minutes, the v0.1 acceptance duration)
#   net_soak.ps1 -Seconds 60
param(
    [int]$Seconds = 600
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $exe = Join-Path (Get-Location) 'build\net_loop.exe'
    if (-not (Test-Path $exe)) { throw 'net_loop.exe not built; run tools/build.ps1' }

    $serverLog = 'build\net_soak_server.log'
    $c0Log = 'build\net_soak_c0.log'
    $c1Log = 'build\net_soak_c1.log'

    $server = Start-Process -FilePath $exe -ArgumentList @(
        '--serve', '--players', '2', '--seconds', "$Seconds"
    ) -PassThru -NoNewWindow -RedirectStandardOutput $serverLog -RedirectStandardError 'build\net_soak_server.err'

    Start-Sleep -Milliseconds 600
    $clientSeconds = $Seconds - 2
    $c0 = Start-Process -FilePath $exe -ArgumentList @(
        '--join', '127.0.0.1', '--slot', '0', '--seconds', "$clientSeconds"
    ) -PassThru -NoNewWindow -RedirectStandardOutput $c0Log -RedirectStandardError 'build\net_soak_c0.err'
    $c1 = Start-Process -FilePath $exe -ArgumentList @(
        '--join', '127.0.0.1', '--slot', '1', '--seconds', "$clientSeconds"
    ) -PassThru -NoNewWindow -RedirectStandardOutput $c1Log -RedirectStandardError 'build\net_soak_c1.err'

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
    $late = if ($stats.Success) { [int]$stats.Groups[2].Value } else { 999 }
    $far = if ($stats.Success) { [int]$stats.Groups[3].Value } else { 999 }

    $clientSnapshots = @()
    foreach ($text in @($c0Text, $c1Text)) {
        $m = [regex]::Match($text, 'final joined=1 snapshots=(\d+)')
        $clientSnapshots += if ($m.Success) { [int]$m.Groups[1].Value } else { 0 }
    }

    # A dead link shows up as silence: the server stops seeing commands and the
    # client stops seeing snapshots. Both counters are checked, not just "no crash".
    $minSnapshots = [math]::Floor(($Seconds - 10) * 20)   # very conservative: 20/s
    $ok = $joins -eq 2 -and $accepted -gt 100 -and $far -eq 0 -and
          ($clientSnapshots[0] -gt $minSnapshots) -and ($clientSnapshots[1] -gt $minSnapshots)

    Write-Host ("soak {0}s joins={1} accepted={2} late={3} far={4} snapshots={5}/{6}" -f `
        $Seconds, $joins, $accepted, $late, $far, $clientSnapshots[0], $clientSnapshots[1])
    if (-not $ok) {
        Write-Host 'FAIL: the link did not stay alive for the whole window' -ForegroundColor Red
        exit 1
    }
    Write-Host 'PASS: two clients stayed connected and kept receiving snapshots' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}