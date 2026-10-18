# smoke.ps1 - the green/red gate. Run this before touching anything.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $exe = 'build\headless_sim.exe'

    Write-Host '== info ==' -ForegroundColor Cyan
    & $exe --info
    if ($LASTEXITCODE -ne 0) { throw 'info failed' }

    Write-Host '== determinism ==' -ForegroundColor Cyan
    & $exe --selftest --ticks 600
    if ($LASTEXITCODE -ne 0) { throw 'determinism selftest failed' }

    Write-Host '== thin wall ==' -ForegroundColor Cyan
    & $exe --test-wall
    if ($LASTEXITCODE -ne 0) { throw 'wall test failed' }

    Write-Host '== ray sweep ==' -ForegroundColor Cyan
    & $exe --test-ray
    if ($LASTEXITCODE -ne 0) { throw 'ray test failed' }

    Write-Host '== steps and crouch ==' -ForegroundColor Cyan
    & $exe --test-step
    if ($LASTEXITCODE -ne 0) { throw 'step test failed' }

    Write-Host '== bench ==' -ForegroundColor Cyan
    & $exe --bench 1000
    if ($LASTEXITCODE -ne 0) { throw 'bench failed' }

    Write-Host 'ALL GREEN' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
