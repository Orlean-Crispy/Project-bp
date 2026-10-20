# check_sources.ps1 - every sim .cpp must be listed exactly once in
# tools/sim_sources.txt, which both CMake and build_bridge.ps1 consume.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $listed = Get-Content 'tools/sim_sources.txt' |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne '' -and -not $_.StartsWith('#') } |
        Sort-Object
    $actual = git ls-files 'sim/**/*.cpp' | ForEach-Object { $_ -replace '\\', '/' } | Sort-Object
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
    $literal = '(27115|27116|27130|27131)'
    $code = git ls-files '*.cpp' '*.h' '*.gd' '*.ps1' |
        Where-Object { $_ -ne 'sim/net/ports.h' -and $_ -ne 'tools/check_sources.ps1' }
    $hits = @(Select-String -Path $code -Pattern $literal -ErrorAction SilentlyContinue |
        ForEach-Object { "$(Split-Path $_.Path -Leaf):$($_.LineNumber): $($_.Line.Trim())" })
    if ($hits.Count -gt 0) {
        Write-Host 'port literal outside sim/net/ports.h:' -ForegroundColor Red
        $hits | ForEach-Object { Write-Host ("  " + $_) }
        exit 1
    }
    Write-Host 'ports ok: literals only in sim/net/ports.h'
}
finally {
    Pop-Location
}
