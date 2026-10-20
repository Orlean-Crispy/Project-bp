# godot_env.ps1 - the one place that knows where Godot lives and how to prepare
# the project. Dot-source it:
#
#   . (Join-Path $PSScriptRoot 'godot_env.ps1')
#
# Before this file existed the executable path and the "first run: import the
# project" block were copied into every launcher, so a Godot version bump meant
# editing several scripts (and forgetting one). $env:BP_GODOT wins, otherwise the
# newest Godot in the toolchain directory is used.
$ErrorActionPreference = 'Stop'

$script:BpRoot = Split-Path -Parent $PSScriptRoot
$script:BpGodot = $null

function Get-BpGodot {
    if ($script:BpGodot) { return $script:BpGodot }
    $candidates = @()
    if ($env:BP_GODOT) { $candidates += $env:BP_GODOT }
    $toolchain = 'C:\bp-toolchain\godot'
    if (Test-Path $toolchain) {
        $candidates += @(Get-ChildItem -Path $toolchain -Filter 'Godot_v*_win64.exe' |
            Sort-Object Name -Descending | ForEach-Object { $_.FullName })
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            $script:BpGodot = $candidate
            return $candidate
        }
    }
    throw 'Godot not found: set $env:BP_GODOT or put it in C:\bp-toolchain\godot'
}

function Get-BpProject {
    return (Join-Path $script:BpRoot 'godot')
}

# A fresh checkout has no godot/.godot, and neither the game nor a headless check
# can load the project before the editor has imported it once.
function Initialize-BpProject {
    $project = Get-BpProject
    if (-not (Test-Path (Join-Path $project '.godot'))) {
        Write-Host 'first run: importing project'
        & (Get-BpGodot) --headless --editor --path $project --quit
        if ($LASTEXITCODE -ne 0) { throw 'Godot project import failed' }
    }
    return $project
}

# Runs the game headless with a hard timeout. A broken GDExtension makes main.gd
# fail to parse, so --hud-check never reaches get_tree().quit() and the process
# would hang forever; the timeout turns that into a readable failure.
function Invoke-BpGodotHeadless {
    param(
        [string[]]$Arguments,
        [int]$TimeoutSeconds = 60
    )
    $project = Initialize-BpProject
    $log = [System.IO.Path]::GetTempFileName()
    $err = $log + '.err'
    $godotArguments = @('--headless', '--path', $project, '--') + $Arguments
    $process = Start-Process -FilePath (Get-BpGodot) -ArgumentList $godotArguments -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError $err
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw ('Godot did not exit within ' + $TimeoutSeconds + ' s; log: ' + $log)
    }
    $result = [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output   = (Get-Content -LiteralPath $log -Raw)
        Error    = (Get-Content -LiteralPath $err -Raw)
    }
    Remove-Item -LiteralPath $log, $err -Force -ErrorAction SilentlyContinue
    return $result
}
