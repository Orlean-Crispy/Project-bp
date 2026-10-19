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

    Write-Host '== jump edge ==' -ForegroundColor Cyan
    & $exe --test-jump
    if ($LASTEXITCODE -ne 0) { throw 'jump edge test failed' }

    Write-Host '== health and armour ==' -ForegroundColor Cyan
    & $exe --test-health
    if ($LASTEXITCODE -ne 0) { throw 'health test failed' }

    Write-Host '== network ==' -ForegroundColor Cyan
    & $exe --test-net
    if ($LASTEXITCODE -ne 0) { throw 'network test failed' }

    Write-Host '== stuck recovery ==' -ForegroundColor Cyan
    & $exe --test-stuck
    if ($LASTEXITCODE -ne 0) { throw 'stuck recovery test failed' }
    & $exe --test-soak
    if ($LASTEXITCODE -ne 0) { throw 'soak test failed' }

    Write-Host '== ledges ==' -ForegroundColor Cyan
    & $exe --test-edge
    if ($LASTEXITCODE -ne 0) { throw 'ledge test failed' }

    Write-Host '== parkour ==' -ForegroundColor Cyan
    & $exe --test-parkour
    if ($LASTEXITCODE -ne 0) { throw 'parkour test failed' }

    Write-Host '== keybinds ==' -ForegroundColor Cyan
    & $exe --test-bindings
    if ($LASTEXITCODE -ne 0) { throw 'keybind test failed' }

    Write-Host '== demo replay ==' -ForegroundColor Cyan
    & $exe --test-demo
    if ($LASTEXITCODE -ne 0) { throw 'demo test failed' }

    Write-Host '== hitbox history ==' -ForegroundColor Cyan
    & $exe --test-hitbox
    if ($LASTEXITCODE -ne 0) { throw 'hitbox history test failed' }

    Write-Host '== event channel ==' -ForegroundColor Cyan
    & $exe --test-events
    if ($LASTEXITCODE -ne 0) { throw 'event channel test failed' }

    Write-Host '== bench ==' -ForegroundColor Cyan
    & $exe --bench 1000
    if ($LASTEXITCODE -ne 0) { throw 'bench failed' }

    Write-Host 'ALL GREEN' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
