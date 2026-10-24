# smoke.ps1 - the green/red gate. Run this before touching anything.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $exe = 'build\headless_sim.exe'

    Write-Host '== sources ==' -ForegroundColor Cyan
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check_sources.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'source list mismatch' }

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

    Write-Host '== weapons ==' -ForegroundColor Cyan
    # The weapon table is the first thing a designer edits. A typo there, or an
    # rpm of zero, must fail here instead of becoming a silent default in a match.
    & $exe --test-weapons
    if ($LASTEXITCODE -ne 0) { throw 'weapon data test failed' }

    Write-Host '== protocol wire ==' -ForegroundColor Cyan
    # The snapshot format is the one thing both sides must agree on byte for byte;
    # it gets its own step so a mask that narrows again fails here, not in a match.
    & $exe --test-snapshot
    if ($LASTEXITCODE -ne 0) { throw 'snapshot wire test failed' }

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

    Write-Host '== godot bridge ==' -ForegroundColor Cyan
    # The headless-sim gates above cannot see the bridge at all: if the DLL fails
    # to load, every Godot check is meaningless and nothing else here notices.
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'godot_check.ps1')
    if ($LASTEXITCODE -eq 3) {
        Write-Host 'SKIP: godot check (Godot or bridge DLL missing)' -ForegroundColor Yellow
    } elseif ($LASTEXITCODE -ne 0) {
        throw 'godot bridge check failed'
    }

    Write-Host '== docs ==' -ForegroundColor Cyan
    # Drift guard: protocol version, gate count, DEBT ledger conservation, dead
    # paths, STATE quality. It reports in English on purpose (PowerShell 5.1
    # mangles CJK output, and a gate nobody can read is a gate nobody trusts).
    & python (Join-Path $PSScriptRoot 'doc_check.py') --root $root
    if ($LASTEXITCODE -ne 0) { throw 'documentation drift detected' }

    Write-Host '== bench ==' -ForegroundColor Cyan
    & $exe --bench 1000
    if ($LASTEXITCODE -ne 0) { throw 'bench failed' }

    Write-Host 'ALL GREEN' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
