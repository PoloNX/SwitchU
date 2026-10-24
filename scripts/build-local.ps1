# Local SwitchU build for Windows + devkitPro + xmake.
# First run can take ~45 minutes (Atmosphere). Later runs are incremental.
param(
    [ValidateSet("release", "debug")]
    [string]$Mode = "release",
    [string]$OutDir = "E:\Desktop\SwitchU\build\sd_out"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DevkitPro = "C:\devkitPro"
$Xmake = "C:\Program Files\xmake"
$CmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if (-not (Test-Path "$CmakeDir\cmake.exe")) {
    $CmakeDir = "C:\Program Files\CMake\bin"
}

if (-not (Test-Path "$DevkitPro\devkitA64\bin\aarch64-none-elf-g++.exe")) {
    throw "devkitPro not found at $DevkitPro"
}
if (-not (Test-Path "$Xmake\xmake.exe")) {
    throw "xmake not found at $Xmake"
}

# Windows GCC treats /opt/devkitpro as C:\opt\devkitpro. Junction that to the real install.
if (-not (Test-Path "C:\opt")) { New-Item -ItemType Directory -Path "C:\opt" | Out-Null }
if (-not (Test-Path "C:\opt\devkitpro")) {
    cmd /c mklink /J "C:\opt\devkitpro" $DevkitPro | Out-Null
}

$env:DEVKITPRO = $DevkitPro
$env:DEVKITARM = Join-Path $DevkitPro "devkitARM"
$env:C_INCLUDE_PATH = Join-Path $DevkitPro "portlibs\switch\include"
$env:CPLUS_INCLUDE_PATH = $env:C_INCLUDE_PATH
$env:LIBRARY_PATH = "$(Join-Path $DevkitPro 'portlibs\switch\lib');$(Join-Path $DevkitPro 'libnx\lib')"
$env:LDFLAGS = "-LC:/devkitPro/portlibs/switch/lib -LC:/devkitPro/libnx/lib"
$env:Path = @(
    $Xmake,
    $CmakeDir,
    "C:\Program Files\Git\cmd",
    "$DevkitPro\devkitA64\bin",
    "$DevkitPro\tools\bin",
    "$DevkitPro\portlibs\switch\bin",
    "$DevkitPro\msys2\usr\bin",
    $env:Path
) -join ";"

Set-Location $RepoRoot

Write-Host "Configuring $Mode sysmodule build..."
& "$Xmake\xmake.exe" f --yes -p cross -m $Mode -a aarch64 --toolchain=devkita64 --homebrew=n --backend=deko3d `
    --ldflags="-LC:/devkitPro/portlibs/switch/lib -LC:/devkitPro/libnx/lib" `
    --cxflags="-IC:/devkitPro/portlibs/switch/include"
if ($LASTEXITCODE -ne 0) { throw "xmake configure failed" }

Write-Host "Building..."
& "$Xmake\xmake.exe"
if ($LASTEXITCODE -ne 0) { throw "xmake build failed" }

$Dist = Join-Path $RepoRoot "dist\local\$Mode"
Write-Host "Packaging to $Dist"
& "$Xmake\xmake.exe" install -o $Dist
if ($LASTEXITCODE -ne 0) { throw "xmake install failed" }

if ($OutDir) {
    if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
    New-Item -ItemType Directory -Path $OutDir | Out-Null
    Copy-Item (Join-Path $Dist "atmosphere") $OutDir -Recurse
    Copy-Item (Join-Path $Dist "switch") $OutDir -Recurse
    Write-Host "Ready to copy to SD: $OutDir"
}
