# setup_toolchain.ps1 - restores the portable toolchain at C:\bp-toolchain.
# It cannot live under the project root: the project path contains non-ASCII
# characters and GNU ld cannot open its own crt/lib files through such a path.
#
# Only this file knows the download layout (AUD-19). Everything else - build.ps1,
# build_bridge.ps1, the docs - uses these stable names, so bumping a tool version
# changes one url line here and nothing else:
#
#   C:\bp-toolchain\mingw64\bin\g++.exe
#   C:\bp-toolchain\cmake\bin\cmake.exe
#   C:\bp-toolchain\ninja\ninja.exe
#
# Extraction uses Expand-Archive, which ships with PowerShell 5.1. 7z is not
# installed on this machine, so it must never be a requirement again.
# Re-running is safe: a present marker file skips the download.
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$tc = 'C:\bp-toolchain'
New-Item -ItemType Directory -Force -Path $tc | Out-Null

# name = zip file base name, dir = stable directory, marker = proof of a complete
# install (relative to $tc), url = the exact release, never "latest".
$items = @(
    @{
        name = 'mingw'; dir = 'mingw64'; marker = 'mingw64\bin\g++.exe'
        url = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r3.zip'
    },
    @{
        name = 'cmake'; dir = 'cmake'; marker = 'cmake\bin\cmake.exe'
        url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'
    },
    @{
        name = 'ninja'; dir = 'ninja'; marker = 'ninja\ninja.exe'
        url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
    }
)

# Older installs kept the vendor directory name (cmake-4.4.4-windows-x86_64).
# Rename instead of re-downloading, so an existing machine keeps working after
# the layout was pinned to version-free names.
function Adopt-Legacy([string]$stable, [string]$pattern) {
    $target = Join-Path $tc $stable
    if (Test-Path $target) { return }
    $legacy = @(Get-ChildItem -Path $tc -Directory -Filter $pattern -ErrorAction SilentlyContinue)
    if ($legacy.Count -eq 1) {
        Write-Host ('adopting ' + $legacy[0].Name + ' as ' + $stable)
        Move-Item -LiteralPath $legacy[0].FullName -Destination $target
    }
}

# Extract to a scratch directory and then move the single vendor wrapper folder
# away, so the stable name is the only name. A zip with several top-level entries
# (ninja-win.zip is just ninja.exe) is moved file by file.
function Expand-Zip([string]$zip, [string]$dest) {
    $tmp = Join-Path $tc '_extract'
    if (Test-Path $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
    $top = @(Get-ChildItem -LiteralPath $tmp)
    if ($top.Count -eq 0) { throw ('empty archive: ' + $zip) }
    if (Test-Path $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    if ($top.Count -eq 1 -and $top[0].PSIsContainer) {
        Move-Item -LiteralPath $top[0].FullName -Destination $dest
    } else {
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Get-ChildItem -LiteralPath $tmp | Move-Item -Destination $dest
    }
    Remove-Item -LiteralPath $tmp -Recurse -Force
}

# A captive portal or a 404 page is not a zip; Expand-Archive would only say
# "not a supported archive format", so fail with the actual cause.
function Assert-Zip([string]$zip) {
    $bytes = New-Object byte[] 2
    $fs = [System.IO.File]::OpenRead($zip)
    try { $null = $fs.Read($bytes, 0, 2) } finally { $fs.Close() }
    if ($bytes[0] -ne 0x50 -or $bytes[1] -ne 0x4B) {
        throw ($zip + ' is not a zip archive (truncated or an error page); delete it and re-run')
    }
}

Adopt-Legacy -stable 'cmake' -pattern 'cmake-*-windows-*'

foreach ($item in $items) {
    $marker = Join-Path $tc $item.marker
    if (Test-Path $marker) {
        Write-Host ($item.name + ' already installed')
        continue
    }
    $zip = Join-Path $tc ($item.name + '.zip')
    if (Test-Path $zip) {
        Assert-Zip $zip
    } else {
        Write-Host ('downloading ' + $item.name)
        (New-Object System.Net.WebClient).DownloadFile($item.url, $zip)
        Assert-Zip $zip
    }
    Write-Host ('extracting ' + $item.name)
    Expand-Zip $zip (Join-Path $tc $item.dir)
    if (-not (Test-Path $marker)) { throw ($item.name + ' extracted but ' + $item.marker + ' is missing') }
}

$gpp = Join-Path $tc 'mingw64\bin\g++.exe'
$cmake = Join-Path $tc 'cmake\bin\cmake.exe'
$ninja = Join-Path $tc 'ninja\ninja.exe'
foreach ($exe in @($gpp, $cmake, $ninja)) {
    if (-not (Test-Path $exe)) { throw ('toolchain incomplete: ' + $exe + ' is missing') }
}
& $gpp --version | Select-Object -First 1
& $cmake --version | Select-Object -First 1
Write-Host ('ninja ' + (& $ninja --version))
Write-Host 'toolchain ready at C:\bp-toolchain'
