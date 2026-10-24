# check_sources.ps1 - the lint that keeps the hard rules from rotting:
#   1. every sim .cpp is listed exactly once in tools/sim_sources.txt (including
#      files that are not committed yet);
#   2. a port number may only be written in sim/net/ports.h;
#   3. sim/ never includes Godot, never converts units outside map_parse.cpp and
#      never uses a stateful RNG (ticket.cpp's key seeding is the one exception).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $listed = Get-Content 'tools/sim_sources.txt' |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne '' -and -not $_.StartsWith('#') } |
        Sort-Object
    # --others: a new sim/*.cpp that is not committed yet must fail here too,
    # otherwise the first build of a fresh file is "listed nowhere" and a stale
    # list only breaks CI later.
    $tracked = git ls-files 'sim/**/*.cpp'
    $untracked = git ls-files --others --exclude-standard 'sim/**/*.cpp'
    $actual = @($tracked) + @($untracked) | ForEach-Object { $_ -replace '\\', '/' } | Sort-Object
    $diff = Compare-Object -ReferenceObject $listed -DifferenceObject $actual
    if ($diff) {
        Write-Host 'source list mismatch (<= listed only, => file only):' -ForegroundColor Red
        $diff | ForEach-Object { Write-Host ("  {0} {1}" -f $_.SideIndicator, $_.InputObject) }
        exit 1
    }
    Write-Host "sources ok: $($listed.Count) sim .cpp files"

    # AUD-14: a port number may only be written in sim/net/ports.h. It used to be
    # 27115 in the menu, 27016 in net_loop and 27130 in the gate script, and a
    # stale one is invisible until a host and a client disagree about where to be.
    $code = @(git ls-files '*.cpp' '*.h' '*.gd' '*.ps1') +
        @(git ls-files --others --exclude-standard '*.cpp' '*.h' '*.gd' '*.ps1') |
        Where-Object { $_ -ne 'sim/net/ports.h' -and $_ -ne 'tools/check_sources.ps1' } |
        Sort-Object -Unique
    $hits = @(Select-String -Path $code -Pattern '\b27[0-9]{3}\b' -ErrorAction SilentlyContinue |
        ForEach-Object { "$(Split-Path $_.Path -Leaf):$($_.LineNumber): $($_.Line.Trim())" })
    if ($hits.Count -gt 0) {
        Write-Host 'port literal outside sim/net/ports.h:' -ForegroundColor Red
        $hits | ForEach-Object { Write-Host ("  " + $_) }
        exit 1
    }
    Write-Host 'ports ok: literals only in sim/net/ports.h'

    # Licence hygiene (ADR 0007): GPL code must never enter the build. A pasted
    # file usually keeps its header, so the header is what we look for. This script
    # is skipped because it contains the pattern itself.
    $gplPattern = 'GNU (Lesser |Affero )?General Public License|SPDX-License-Identifier:\s*(GPL|AGPL)'
    $gplScan = @($code | Where-Object { $_ -ne 'tools/check_sources.ps1' })
    $gplHits = @($gplScan |
        Select-String -Pattern $gplPattern -ErrorAction SilentlyContinue |
        ForEach-Object { "$(Split-Path $_.Path -Leaf):$($_.LineNumber)" })
    if ($gplHits.Count -gt 0) {
        Write-Host 'GPL/AGPL source in the tree (ADR 0007: read facts, write our own code):' -ForegroundColor Red
        $gplHits | ForEach-Object { Write-Host ("  " + $_) }
        exit 1
    }
    Write-Host 'licences ok: no GPL/AGPL source headers in the tree'

    # Iron law A/B/C: the three rules that are cheap to check mechanically. They
    # were only in the docs, so nothing failed when the bridge did its own unit
    # conversion (2026-10-20).
    $simFiles = @(git ls-files 'sim/**/*.cpp' 'sim/**/*.h')
    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($file in $simFiles) {
        $text = Get-Content -LiteralPath $file -Raw
        # Comments talk *about* the rules ("no rand(), no mt19937"), so strip them
        # before matching or the lint fails on its own documentation.
        $text = [regex]::Replace($text, '/\*.*?\*/', '')
        $text = [regex]::Replace($text, '//[^\r\n]*', '')
        if ($text -match '#include\s*[<"]godot') {
            $violations.Add("$file includes a Godot header (iron law C)")
        }
        if ($file -ne 'sim/collision/map_parse.cpp' -and $text -match '0\.0254') {
            $violations.Add("$file converts Quake units (iron law A: only map_parse.cpp)")
        }
        if ($text -match 'std::rand\s*\(|mt19937|random_device' -and $file -ne 'sim/net/ticket.cpp') {
            $violations.Add("$file uses a stateful RNG (iron law B: use rng_at)")
        }
    }
    if ($violations.Count -gt 0) {
        Write-Host 'hard-rule violations:' -ForegroundColor Red
        $violations | ForEach-Object { Write-Host ("  " + $_) }
        exit 1
    }
    Write-Host 'rules ok: no Godot in sim, one unit conversion point, no simulation RNG'
}
finally {
    Pop-Location
}