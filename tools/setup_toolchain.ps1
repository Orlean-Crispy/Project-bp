# setup_toolchain.ps1 - restores the portable toolchain at C:\bp-toolchain.
# It cannot live under the project root: the project path contains non-ASCII
# characters and GNU ld cannot open its own crt/lib files through such a path.
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$tc = 'C:\bp-toolchain'
New-Item -ItemType Directory -Force -Path $tc | Out-Null

$items = @(
    @{ n = 'mingw'; url = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r3.zip'; dir = '.' },
    @{ n = 'cmake'; url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'; dir = '.' },
    @{ n = 'ninja'; url = 'https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip'; dir = 'ninja' }
)

foreach ($i in $items) {
    $zip = Join-Path $tc ($i.n + '.zip')
    if (-not (Test-Path $zip)) {
        Write-Host ("downloading " + $i.n)
        (New-Object System.Net.WebClient).DownloadFile($i.url, $zip)
    }
    $out = Join-Path $tc $i.dir
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    Write-Host ("extracting " + $i.n)
    & 7z x -y ("-o" + $out) $zip | Out-Null
}

& (Join-Path $tc 'mingw64\bin\g++.exe') --version | Select-Object -First 1
Write-Host 'toolchain ready at C:\bp-toolchain'
