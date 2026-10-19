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
}
finally {
    Pop-Location
}
