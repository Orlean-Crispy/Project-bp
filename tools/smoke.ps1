# smoke.ps1 - the green/red gate. Run this before touching anything.
#
# AUD-45: a gate that is skipped is NOT a pass, and a gate that prints FAIL but
# exits 0 is NOT a pass either. Both used to be reported as "ALL GREEN", which
# made the whole list untrustworthy (lesson 15 / 18).
#
#   smoke.ps1                   mandatory Godot gate: a SKIP fails the run
#   smoke.ps1 -AllowGodotSkip   explicit opt-out for a machine without Godot
param(
    [switch]$AllowGodotSkip
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $exe = 'build\headless_sim.exe'

    # The gate list is DATA. tools/doc_check.py counts these entries and compares
    # them with the docs' "22 项闸门", so the table in docs/AGENTS.md cannot drift
    # from what actually runs (it used to, silently).
    $gates = @(
        @{ name = 'info'; args = @('--info') },
        @{ name = 'determinism'; args = @('--selftest', '--ticks', '600') },
        @{ name = 'thin wall'; args = @('--test-wall') },
        @{ name = 'ray sweep'; args = @('--test-ray') },
        @{ name = 'steps and crouch'; args = @('--test-step') },
        @{ name = 'jump edge'; args = @('--test-jump') },
        @{ name = 'health and armour'; args = @('--test-health') },
        # The weapon table is the first thing a designer edits: a typo or an rpm
        # of zero must fail here, not become a silent default in a match.
        @{ name = 'weapons'; args = @('--test-weapons') },
        # E1: aimed at the chest must hit, one box width off must miss.
        @{ name = 'hitscan'; args = @('--test-hitscan') },
        @{ name = 'damage'; args = @('--test-damage') },
        # Byte-for-byte agreement between the two sides of the wire.
        @{ name = 'protocol wire'; args = @('--test-snapshot') },
        @{ name = 'network'; args = @('--test-net') },
        @{ name = 'stuck recovery'; args = @('--test-stuck') },
        @{ name = 'soak'; args = @('--test-soak') },
        @{ name = 'ledges'; args = @('--test-edge') },
        @{ name = 'parkour'; args = @('--test-parkour') },
        @{ name = 'keybinds'; args = @('--test-bindings') },
        @{ name = 'demo replay'; args = @('--test-demo') },
        @{ name = 'hitbox history'; args = @('--test-hitbox') },
        @{ name = 'event channel'; args = @('--test-events') },
        # The headless gates cannot see the bridge: if the DLL fails to load,
        # every Godot check is meaningless and nothing else here notices.
        @{ name = 'godot bridge'; action = 'godot' },
        # Drift guard: protocol version, gate count, DEBT ledger, dead paths.
        @{ name = 'docs'; action = 'docs' }
    )

    # One runner for every gate, so no step can forget half of the contract:
    # the exit code says "no failure", the FAIL marker says "no quiet failure".
    function Invoke-Gate([string]$name, [string[]]$arguments) {
        Write-Host ('== ' + $name + ' ==') -ForegroundColor Cyan
        $text = (& $exe @arguments 2>&1 | Out-String)
        $code = $LASTEXITCODE
        if ($text.Trim().Length -gt 0) { Write-Host $text.TrimEnd() }
        if ($code -ne 0) { throw ($name + ': exit ' + $code) }
        if ($text -match '(?m)^\s*FAIL') { throw ($name + ': printed FAIL but exited 0') }
    }

    # Preflight, not a numbered gate: every .cpp must still be listed in
    # sim_sources.txt, or a forgotten file silently drops out of both builds.
    Write-Host '== sources ==' -ForegroundColor Cyan
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check_sources.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'source list mismatch' }

    foreach ($gate in $gates) {
        if ($gate.ContainsKey('args')) {
            Invoke-Gate $gate.name $gate.args
            continue
        }
        switch ($gate.action) {
            'godot' {
                Write-Host ('== ' + $gate.name + ' ==') -ForegroundColor Cyan
                $godotText = (& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'godot_check.ps1') 2>&1 | Out-String)
                $godotCode = $LASTEXITCODE
                if ($godotText.Trim().Length -gt 0) { Write-Host $godotText.TrimEnd() }
                if ($godotCode -eq 3) {
                    if (-not $AllowGodotSkip) {
                        throw 'godot bridge check was skipped; it is a mandatory gate. Build the bridge (tools\build_bridge.ps1) or pass -AllowGodotSkip on purpose.'
                    }
                    Write-Host 'SKIP: godot check, allowed by -AllowGodotSkip' -ForegroundColor Yellow
                } elseif ($godotCode -ne 0) {
                    throw ('godot bridge check: exit ' + $godotCode)
                }
                if ($godotText -match '(?m)^FAIL') { throw 'godot bridge check printed FAIL but exited 0' }
            }
            'docs' {
                Write-Host ('== ' + $gate.name + ' ==') -ForegroundColor Cyan
                & python (Join-Path $PSScriptRoot 'doc_check.py') --root $root
                if ($LASTEXITCODE -ne 0) { throw 'documentation drift detected' }
            }
            default { throw ('unknown gate action: ' + $gate.action) }
        }
    }

    Write-Host '== bench ==' -ForegroundColor Cyan
    & $exe --bench 1000
    if ($LASTEXITCODE -ne 0) { throw 'bench failed' }

    Write-Host ('ALL GREEN (' + $gates.Count + ' gates + bench, no skips)') -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
