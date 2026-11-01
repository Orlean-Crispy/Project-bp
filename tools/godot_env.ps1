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
        [int]$TimeoutSeconds = 60,
        # AUD-45: a non-zero exit is a failure unless the caller says otherwise.
        # Swallowing it meant a crashed Godot still "passed" as long as its
        # stdout happened to contain the expected line.
        [switch]$AllowNonZeroExit
    )
    $project = Initialize-BpProject
    $godotArguments = @('--headless', '--path', ('"' + $project + '"'), '--') + $Arguments
    # System.Diagnostics.Process, not Start-Process: in this environment
    # Start-Process -PassThru leaves ExitCode empty, so the exit code was
    # collected but never actually readable (AUD-45).
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = (Get-BpGodot)
    $info.Arguments = ($godotArguments -join ' ')
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'could not start Godot' }
    $outTask = $process.StandardOutput.ReadToEndAsync()
    $errTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $process.Kill() } catch { }
        throw ('Godot did not exit within ' + $TimeoutSeconds + ' s for [' + ($Arguments -join ' ') + ']')
    }
    $process.WaitForExit()
    $result = [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output   = $outTask.Result
        Error    = $errTask.Result
    }
    if ($result.ExitCode -ne 0 -and -not $AllowNonZeroExit) {
        $out = ($result.Output + $result.Error)
        if ($out.Length -gt 600) { $out = $out.Substring($out.Length - 600) }
        throw ('Godot exited with ' + $result.ExitCode + ' for [' + ($Arguments -join ' ') + ']: ' + $out)
    }
    return $result
}
