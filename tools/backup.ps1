# backup.ps1 - make a full-history git bundle outside the working tree.
# A bundle is one file that can be cloned from, so it works as an offline
# backup even when the network cannot reach GitHub.
param(
    [string]$Destination = 'G:\BreakpointBackups'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $bundle = Join-Path $Destination "breakpoint-$stamp.bundle"
    git bundle create $bundle --all
    if ($LASTEXITCODE -ne 0) { throw 'git bundle create failed' }
    git bundle verify $bundle
    if ($LASTEXITCODE -ne 0) { throw 'git bundle verify failed' }
    $mb = [math]::Round((Get-Item -LiteralPath $bundle).Length / 1MB, 2)
    Write-Host "backup ok: $bundle ($mb MB)"
}
finally {
    Pop-Location
}
