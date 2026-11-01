# mutants.ps1 - prove the gates still have teeth (AUD-39).
#
# Each mutant breaks ONE thing on purpose, rebuilds, runs the gate that should
# notice, and expects a non-zero exit. A mutant that passes means the gate became
# decoration. The file is always restored, even when the run fails.
#
#   powershell -File tools\mutants.ps1            # all three
#   powershell -File tools\mutants.ps1 -Only hash-ammo
param(
    [string]$Only = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $mutants = @(
        @{ name = 'hash-ammo'; file = 'sim/core/world.cpp';
           from = '.u32(static_cast<uint32_t>(p.ammo))'; to = '';
           gate = '--test-fields'; why = 'ammo must move state_hash' },
        @{ name = 'snapshot-weapon'; file = 'sim/net/snapshot.cpp';
           from = 'if (!same_weapon(now, before)) mask |= kFieldWeapon;'; to = 'if (false && !same_weapon(now, before)) mask |= kFieldWeapon;';
           gate = '--test-snapshot'; why = 'the Weapon group must reach the wire' },
        @{ name = 'snapshot-hit'; file = 'sim/net/snapshot.cpp';
           from = 'if (!same_hit(now, before)) mask |= kFieldHit;'; to = 'if (false && !same_hit(now, before)) mask |= kFieldHit;';
           gate = '--test-snapshot'; why = 'the Hit group must reach the wire' }
    )

    # Crash recovery: the previous run may have been killed after writing a
    # mutant and before the finally block could restore it (that is exactly what
    # happened once), leaving the tree mutated.
    $backupDir = Join-Path $root 'build\mutants'
    if (Test-Path $backupDir) {
        Get-ChildItem $backupDir -Filter '*.orig' | ForEach-Object {
            $target = Join-Path $root ($_.BaseName + '.src')
            # The backup name encodes the path with '/' replaced by '~'.
            $relative = $_.BaseName.Replace('~', '/')
            $target = Join-Path $root $relative
            if (Test-Path $target) {
                [System.IO.File]::WriteAllText($target, [System.IO.File]::ReadAllText($_.FullName))
                Write-Host ('restored leftover mutant: ' + $relative) -ForegroundColor Yellow
            }
            Remove-Item -LiteralPath $_.FullName -Force
        }
    }
    if (-not (Test-Path $backupDir)) { New-Item -ItemType Directory -Path $backupDir | Out-Null }

    $failures = 0
    foreach ($mutant in $mutants) {
        if ($Only -ne '' -and $Only -ne $mutant.name) { continue }
        $path = Join-Path $root $mutant.file
        $original = [System.IO.File]::ReadAllText($path)
        if (-not $original.Contains($mutant.from)) {
            Write-Host ('STALE: ' + $mutant.name + ' no longer matches its source line') -ForegroundColor Red
            ++$failures
            continue
        }
        $backup = Join-Path $backupDir (($mutant.file.Replace('/', '~')) + '.orig')
        [System.IO.File]::WriteAllText($backup, $original)
        try {
            [System.IO.File]::WriteAllText($path, $original.Replace($mutant.from, $mutant.to))
            $build = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') 2>&1
            if ($LASTEXITCODE -ne 0) {
                # A mutant that does not compile proves nothing about the gate.
                Write-Host ('FAIL: ' + $mutant.name + ' did not compile, so nothing was tested') -ForegroundColor Red
                ++$failures
                continue
            }
            & build\headless_sim.exe $mutant.gate *> build\mutant_gate.log
            $code = $LASTEXITCODE
            if ($code -eq 0) {
                Write-Host ('FAIL: ' + $mutant.name + ' survived ' + $mutant.gate + ' - the gate is decoration') -ForegroundColor Red
                ++$failures
            } else {
                Write-Host ('ok: ' + $mutant.name + ' caught by ' + $mutant.gate + ' (' + $mutant.why + ')') -ForegroundColor Green
            }
        } finally {
            [System.IO.File]::WriteAllText($path, $original)
            Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
        }
    }

    # Leave the tree built from the restored sources, not from a mutant.
    $restore = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'rebuild after restoring the mutants failed' }

    if ($failures -ne 0) {
        Write-Host ('FAIL: ' + $failures + ' mutant(s) not caught') -ForegroundColor Red
        exit 1
    }
    Write-Host 'PASS: every mutant was caught by the gate that should catch it' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}