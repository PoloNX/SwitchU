<#
.SYNOPSIS
    Builds SwitchU with xmake, skipping every step that is already up to date.

.DESCRIPTION
    Caching happens at four levels:
      1. xmake is reconfigured only when the requested configuration changes.
      2. Each variant gets its own build directory (build/<variant>), so
         switching between sysmodule and homebrew does not recompile
         everything; xmake's incremental build and compile cache do the rest.
      3. The eSpeak NG data and libstratosphere are regenerated only when
         their submodule changes (see scripts/stamp.lua).
      4. When no source file changed since the last successful build of the
         same configuration, xmake is not started at all.

    The sysmodule build is installed to sd_out/ in the SD card layout after
    every build (skipped when it is already up to date). Copy sd_out/ to the
    root of the SD card.

.EXAMPLE
    .\build.ps1                          # sysmodule, release, installed to sd_out/
.EXAMPLE
    .\build.ps1 -NoInstall               # sysmodule without touching sd_out/
.EXAMPLE
    .\build.ps1 -Variant homebrew        # standalone .nro for testing
.EXAMPLE
    .\build.ps1 -Variant homebrew -Install   # also stage it in dist/homebrew-<mode>
.EXAMPLE
    .\build.ps1 -Force                   # bypass the "nothing changed" shortcut
.EXAMPLE
    .\build.ps1 -Clean                   # drop this variant's build and all stamps
#>
[CmdletBinding()]
param(
    [ValidateSet('sysmodule', 'homebrew')]
    [string]$Variant = 'sysmodule',

    [ValidateSet('release', 'debug')]
    [string]$Mode = 'release',

    [ValidateSet('deko3d', 'sdl2')]
    [string]$Backend = 'deko3d',

    # Build a single xmake target (SwitchU, SwitchU-Manager, switchu-daemon).
    [string]$Target = '',

    # Parallel jobs; 0 lets xmake decide.
    [int]$Jobs = 0,

    # Homebrew only: copy the result to dist/homebrew-<mode> in the SD card
    # layout. The sysmodule is always installed to sd_out/.
    [switch]$Install,

    # Sysmodule only: do not install to sd_out/.
    [switch]$NoInstall,

    # Ignore the "nothing changed" shortcut and let xmake check everything.
    [switch]$Force,

    # Remove this variant's build directory and the generated-data stamps first.
    [switch]$Clean,

    # Pass -v to xmake.
    [switch]$VerboseBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build\$Variant"
$CacheDir = Join-Path $Root 'build\.buildcache'
$StampDir = Join-Path $Root 'build\.stamps'
$ConfigCache = Join-Path $CacheDir 'xmake-config.txt'
$FingerprintFile = Join-Path $CacheDir "$Variant-$Mode.fingerprint"
if ($Variant -eq 'sysmodule') {
    $InstallRelative = 'sd_out'
    $DoInstall = -not $NoInstall
} else {
    $InstallRelative = "dist\$Variant-$Mode"
    $DoInstall = [bool]$Install
}
$InstallDir = Join-Path $Root $InstallRelative
# One fingerprint per destination: switching between release and debug must
# reinstall, since both land in the same sd_out/.
$InstallFingerprintFile = Join-Path $CacheDir (($InstallRelative -replace '\\', '-') + '.install.fingerprint')

function Write-Step([string]$Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Cached([string]$Message) {
    Write-Host "    $Message (cached)" -ForegroundColor DarkGray
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    # Windows PowerShell turns redirected stderr lines into errors; with
    # 'Stop' a compiler warning would abort the build. The exit code decides.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE"
    }
}

function Add-ToolPath([string]$Directory) {
    $parts = $env:PATH -split ';'
    if ($parts -notcontains $Directory) {
        # Appended, not prepended: MSYS tools must not shadow the Windows ones.
        $env:PATH = "$env:PATH;$Directory"
    }
}

function Find-Tool([string]$Name, [string[]]$Candidates, [string]$InstallHint) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            Add-ToolPath (Split-Path $candidate -Parent)
            return $candidate
        }
    }
    throw "$Name not found. $InstallHint"
}

function Get-TextHash([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return [System.BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
}

function Read-Cache([string]$File) {
    if (Test-Path $File) {
        return (Get-Content $File -Raw).Trim()
    }
    return ''
}

function Write-Cache([string]$File, [string]$Value) {
    New-Item -ItemType Directory -Force (Split-Path $File -Parent) | Out-Null
    Set-Content -Path $File -Value $Value -Encoding ascii
}

# Hash of everything that can change the build output: the commit, every
# submodule commit, and the size and timestamp of each modified or untracked
# file. Editing, adding, deleting or reverting a file changes it.
function Get-SourceFingerprint([string]$ConfigKey) {
    $head = (& $git -C $Root rev-parse HEAD)
    $submodules = (& $git -C $Root submodule status --recursive) -join "`n"
    $status = (& $git -C $Root status --porcelain=v1 -z -uall) -join ''
    if ($LASTEXITCODE -ne 0) {
        throw 'git status failed'
    }

    $entries = New-Object System.Collections.Generic.List[string]
    $records = $status -split [char]0
    for ($i = 0; $i -lt $records.Count; $i++) {
        $record = $records[$i]
        if ($record.Length -lt 4) { continue }
        $code = $record.Substring(0, 2)
        $file = $record.Substring(3)
        # Renames and copies are followed by their source path.
        if ($code -match '[RC]') { $i++ }
        $full = Join-Path $Root $file
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            $item = Get-Item -LiteralPath $full
            $entries.Add("$code $file $($item.Length) $($item.LastWriteTimeUtc.Ticks)")
        } else {
            $entries.Add("$code $file")
        }
    }
    $entries.Sort()

    return Get-TextHash (@($ConfigKey, $Target, $head, $submodules) + $entries -join "`n")
}

function Get-ExpectedOutputs {
    $outDir = Join-Path $BuildDir "cross\aarch64\$Mode"
    if ($Target) {
        return @()
    }
    $names = @('SwitchU-Manager.nro')
    if ($Variant -eq 'homebrew') {
        $names += 'SwitchU.nro'
    } else {
        $names += 'switchu-menu.nsp', 'switchu-daemon.nsp'
    }
    return $names | ForEach-Object { Join-Path $outDir $_ }
}

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
Push-Location $Root
try {
    Write-Step "SwitchU $Variant ($Mode, $Backend)"

    # --- Tools --------------------------------------------------------------
    $git = Find-Tool 'git' @(
        (Join-Path $env:ProgramFiles 'Git\cmd\git.exe'),
        'C:\msys64\usr\bin\git.exe'
    ) 'Install Git for Windows.'
    $xmake = Find-Tool 'xmake' @(
        (Join-Path $env:ProgramFiles 'xmake\xmake.exe'),
        (Join-Path $env:LOCALAPPDATA '.xmake\bin\xmake.exe')
    ) 'Install xmake from https://xmake.io.'

    if (-not ($env:DEVKITPRO -and (Test-Path $env:DEVKITPRO))) {
        $devkitPro = @('C:\devkitPro', 'C:\msys64\opt\devkitpro') |
            Where-Object { Test-Path (Join-Path $_ 'devkitA64') } |
            Select-Object -First 1
        if (-not $devkitPro) {
            throw 'devkitPro not found. Install it or set DEVKITPRO.'
        }
        $env:DEVKITPRO = $devkitPro -replace '\\', '/'
    }
    $msysBin = @(
        (Join-Path $env:DEVKITPRO 'msys2\usr\bin'),
        'C:\msys64\usr\bin'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    # cmake builds the eSpeak NG data, make builds eSpeak and libstratosphere.
    Find-Tool 'cmake' @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe')
    ) 'Install CMake from https://cmake.org.' | Out-Null
    $makeCandidates = @()
    if ($msysBin) { $makeCandidates += (Join-Path $msysBin 'make.exe') }
    Find-Tool 'make' $makeCandidates 'Install make with MSYS2 (pacman -S make).' | Out-Null
    if ($msysBin) {
        # The "MSYS Makefiles" generator also needs sh.
        Add-ToolPath $msysBin
    }

    # --- Submodules ---------------------------------------------------------
    $missing = @('lib\espeak-ng', 'lib\Atmosphere-libs') |
        Where-Object { -not (Test-Path (Join-Path $Root "$_\.git")) }
    if ($missing) {
        Write-Step 'Fetching submodules'
        Invoke-Native $git @('-C', $Root, 'submodule', 'update', '--init', '--recursive')
    }

    # --- Clean --------------------------------------------------------------
    if ($Clean) {
        Write-Step "Cleaning build\$Variant"
        foreach ($dir in @($BuildDir, $StampDir)) {
            if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        }
        foreach ($file in @($FingerprintFile, $InstallFingerprintFile)) {
            if (Test-Path $file) { Remove-Item -Force $file }
        }
    }

    # --- Configure ----------------------------------------------------------
    $homebrewFlag = 'n'
    if ($Variant -eq 'homebrew') { $homebrewFlag = 'y' }
    $configArgs = @(
        'f', '--yes', '-p', 'cross', '-a', 'aarch64', '--toolchain=devkita64',
        '-m', $Mode, "--homebrew=$homebrewFlag", "--backend=$Backend",
        '-o', "build/$Variant"
    )
    $configKey = $configArgs -join ' '
    $configFile = Get-ChildItem (Join-Path $Root '.xmake') -Recurse -Filter 'xmake.conf' `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ((Read-Cache $ConfigCache) -eq $configKey -and $configFile) {
        Write-Cached 'xmake configuration'
    } else {
        Write-Step 'Configuring xmake'
        # Drop the key first so a failed configure is retried next time.
        if (Test-Path $ConfigCache) { Remove-Item -Force $ConfigCache }
        Invoke-Native $xmake $configArgs
        Write-Cache $ConfigCache $configKey
    }

    # --- Build --------------------------------------------------------------
    $fingerprint = Get-SourceFingerprint $configKey
    $outputs = @(Get-ExpectedOutputs)
    $outputsPresent = -not ($outputs | Where-Object { -not (Test-Path $_) })
    $upToDate = (-not $Force) -and $outputsPresent -and
        ((Read-Cache $FingerprintFile) -eq $fingerprint)

    if ($upToDate) {
        Write-Cached 'no source change since the last build'
    } else {
        Write-Step 'Building'
        # Forget the previous success so an interrupted build is not trusted.
        if (Test-Path $FingerprintFile) { Remove-Item -Force $FingerprintFile }
        $buildArgs = @('build', '-y')
        if ($Jobs -gt 0) { $buildArgs += @('-j', "$Jobs") }
        if ($VerboseBuild) { $buildArgs += '-v' }
        if ($Target) { $buildArgs += $Target }
        Invoke-Native $xmake $buildArgs
        Write-Cache $FingerprintFile $fingerprint
    }

    # --- Install ------------------------------------------------------------
    if ($DoInstall) {
        if ((Test-Path $InstallDir) -and
            (Read-Cache $InstallFingerprintFile) -eq $fingerprint) {
            Write-Cached "install to $InstallRelative"
        } else {
            Write-Step "Installing to $InstallRelative"
            if (Test-Path $InstallFingerprintFile) { Remove-Item -Force $InstallFingerprintFile }
            $installArgs = @('install', '-o', $InstallDir)
            if ($Target) { $installArgs += $Target }
            Invoke-Native $xmake $installArgs
            Write-Cache $InstallFingerprintFile $fingerprint
        }
    }

    $stopwatch.Stop()
    $summary = "Done in {0:N1}s. Outputs: build\{1}\cross\aarch64\{2}" -f `
        $stopwatch.Elapsed.TotalSeconds, $Variant, $Mode
    if ($DoInstall) { $summary += ", SD card layout: $InstallRelative" }
    Write-Host $summary -ForegroundColor Green
} finally {
    Pop-Location
}
