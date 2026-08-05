# PHD2 Installer Build Script for Windows (x64)
# This script builds PHD2 and creates an installer using InnoSetup.
#
# This fork builds x64 only. 32-bit Windows support was dropped alongside
# the C++20 / wxWidgets 3.2 modernization; legacy camera SDKs that were the
# original reason to ship x86 are gone in the Alpaca-only build.

$ErrorActionPreference = "Stop"

Write-Host "Building PHD2 Installer (x64)" -ForegroundColor Green

# Determine paths
if ($PSScriptRoot) {
    $RootDir = $PSScriptRoot
} else {
    $RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
}
Set-Location $RootDir

# Extract version from version.md
Write-Host "Extracting version from version.md..." -ForegroundColor Yellow
$versionFilePath = Join-Path $RootDir "version.md"
if (-not (Test-Path $versionFilePath)) {
    Write-Error "Cannot find version.md at: $versionFilePath"
    Write-Host "Current directory: $(Get-Location)" -ForegroundColor Yellow
    exit 1
}
$versionFile = Get-Content $versionFilePath -Raw
$versionMatch = [regex]::Match($versionFile, '(?m)^\s*([0-9]+\.[0-9]+\.[0-9]+([A-Za-z0-9._-]*))\s*$')
if (-not $versionMatch.Success) {
    Write-Error "Could not extract version from version.md (expected line like 1.2.3 or 1.2.3rc1)"
    exit 1
}
$fullVersion = $versionMatch.Groups[1].Value

Write-Host "Detected version: $fullVersion" -ForegroundColor Green

# x64 only
$BuildDir = "tmp"
$CMakeArch = "x64"
$InstallerArch = "-x64"
$InstallerTemplate = "phd2.iss.in"

# Clean build directory to ensure a fresh build each run
if (Test-Path $BuildDir) {
    Write-Host "Cleaning build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Path $BuildDir -Recurse -Force
}

# Check for required tools, bootstrapping missing ones where we can.
Write-Host "Checking for required tools..." -ForegroundColor Yellow

# Best-effort winget install; returns $true if winget is present and the
# install command completed (which includes "already installed").
function Install-WithWinget([string]$PackageId, [string]$DisplayName) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Host "  winget not available; cannot auto-install $DisplayName" -ForegroundColor Yellow
        return $false
    }
    Write-Host "  Installing $DisplayName via winget..." -ForegroundColor Yellow
    & winget install --id $PackageId -e --accept-source-agreements --accept-package-agreements --silent
    return ($LASTEXITCODE -eq 0)
}

# Check git: the CMake configure fetches vcpkg via FetchContent, which needs
# git on PATH.
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    if (Install-WithWinget "Git.Git" "Git") {
        # winget updates the machine PATH, not this process's.
        $gitBin = "C:\Program Files\Git\cmd"
        if (Test-Path (Join-Path $gitBin "git.exe")) { $env:Path = "$env:Path;$gitBin" }
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Error "git not found and auto-install failed. Install Git (winget install Git.Git) and re-run."
        exit 1
    }
}
Write-Host "  Git: $((Get-Command git).Source)" -ForegroundColor Green

# Locate the Visual Studio install once; used for the compiler itself, the
# bundled CMake fallback, and the wxWidgets bootstrap build. On a bare
# machine, install the VS Build Tools with the C++ workload via winget
# (a multi-GB download; expect 10-20 min).
function Find-VSPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        return & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    return $null
}
$vsPath = Find-VSPath
if (-not $vsPath) {
    Write-Host "  No Visual Studio C++ toolchain found; installing VS Build Tools (this is large - 10-20 min)..." -ForegroundColor Yellow
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        & winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --silent --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools;includeRecommended"
        $vsPath = Find-VSPath
    }
    if (-not $vsPath) {
        Write-Error "No Visual Studio with the C++ toolchain found and auto-install failed. Install VS Build Tools or VS Community with 'Desktop development with C++' and re-run."
        exit 1
    }
}
Write-Host "  Visual Studio: $vsPath" -ForegroundColor Green

# Check CMake: PATH, then default install dir, then the copy bundled with
# Visual Studio, then winget as a last resort.
function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @("C:\Program Files\CMake\bin\cmake.exe")
    if ($vsPath) {
        $candidates += (Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    }
    return $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
$cmakePath = Find-CMake
if (-not $cmakePath) {
    if (Install-WithWinget "Kitware.CMake" "CMake") {
        # winget updates the machine PATH, not this process's; re-scan known dirs.
        $cmakePath = Find-CMake
    }
    if (-not $cmakePath) {
        Write-Error "CMake not found and auto-install failed. Install CMake (winget install Kitware.CMake) and re-run."
        exit 1
    }
}
Write-Host "  CMake: $cmakePath" -ForegroundColor Green

# Check InnoSetup. Prefer Inno Setup 6 (current release) over the legacy 5,
# and look under both the 64-bit and 32-bit Program Files trees. Auto-install
# via winget when absent.
$isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
    "C:\Program Files\Inno Setup 5\ISCC.exe"
)
$isccPath = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $isccPath) {
    if (Install-WithWinget "JRSoftware.InnoSetup" "Inno Setup 6") {
        $isccPath = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if (-not $isccPath) {
        Write-Error "InnoSetup not found and auto-install failed. Install Inno Setup 6 (winget install JRSoftware.InnoSetup) and re-run."
        Write-Host "  Searched:" -ForegroundColor Yellow
        foreach ($c in $isccCandidates) { Write-Host "    $c" -ForegroundColor Yellow }
        exit 1
    }
}
Write-Host "  InnoSetup: $isccPath" -ForegroundColor Green

# Check WXWIN. thirdparty.cmake hard-fails the configure step unless WXWIN
# points at a wxWidgets install with a static vc_x64_lib build. If it's not
# set up, download and build wxWidgets the same way CI does (release.yml):
# pinned version + SHA256, static libs, dynamic CRT (/MD) to match vcpkg.
$WxBootstrapVersion = "3.2.11"
# SHA256 of the official wxWidgets-<version>.zip release asset. Leave empty to
# trust-on-first-use: the script prints the computed hash so it can be pinned
# here afterwards. When bumping the version, clear this, run once, and pin the
# printed value.
$WxBootstrapHash = ""
$WxBootstrapDir = "C:\wxWidgets"

function Test-WxDir([string]$dir) {
    return $dir -and (Test-Path (Join-Path $dir "lib\vc_x64_lib") -PathType Container)
}

if (-not (Test-WxDir $env:WXWIN)) {
    if ($env:WXWIN) {
        Write-Host "  WXWIN='$env:WXWIN' has no static x64 libs at lib\vc_x64_lib" -ForegroundColor Yellow
    } else {
        Write-Host "  WXWIN is not set" -ForegroundColor Yellow
    }

    if (Test-WxDir $WxBootstrapDir) {
        # A previous bootstrap (or the CI cache layout) is already there.
        $env:WXWIN = $WxBootstrapDir
    } else {
        if (-not $vsPath) {
            Write-Error "Cannot bootstrap wxWidgets: no Visual Studio with the C++ toolchain found. Install VS (Community is fine) with 'Desktop development with C++' and re-run."
            exit 1
        }
        Write-Host "  Bootstrapping wxWidgets $WxBootstrapVersion (static x64) into $WxBootstrapDir - this takes 20-40 min, one time only" -ForegroundColor Yellow
        $wxZip = Join-Path $env:TEMP "wxWidgets-$WxBootstrapVersion.zip"
        Invoke-WebRequest -Uri "https://github.com/wxWidgets/wxWidgets/releases/download/v$WxBootstrapVersion/wxWidgets-$WxBootstrapVersion.zip" -OutFile $wxZip
        $actualHash = (Get-FileHash -Algorithm SHA256 $wxZip).Hash.ToLowerInvariant()
        if ($WxBootstrapHash) {
            if ($actualHash -ne $WxBootstrapHash) {
                Write-Error "wxWidgets download hash mismatch: expected $WxBootstrapHash, got $actualHash"
                exit 1
            }
        } else {
            Write-Host "  wxWidgets $WxBootstrapVersion SHA256 (unpinned, trust-on-first-use): $actualHash" -ForegroundColor Yellow
            Write-Host "  Pin it by setting `$WxBootstrapHash in build-exe.ps1 to that value." -ForegroundColor Yellow
        }
        Expand-Archive $wxZip -DestinationPath $WxBootstrapDir
        cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" && cd /d $WxBootstrapDir\build\msw && nmake /f makefile.vc BUILD=release TARGET_CPU=X64"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "wxWidgets bootstrap build failed"
            exit 1
        }
        $env:WXWIN = $WxBootstrapDir
        Write-Host "  wxWidgets built. Consider setting WXWIN=$WxBootstrapDir permanently (setx WXWIN $WxBootstrapDir) to skip this check next time." -ForegroundColor Yellow
    }
}
Write-Host "  WXWIN: $env:WXWIN" -ForegroundColor Green

# Create build directory
Write-Host "Creating build directory: $BuildDir" -ForegroundColor Yellow
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Set-Location $BuildDir

# Configure CMake
Write-Host "Configuring CMake for $CMakeArch..." -ForegroundColor Yellow
$cmakeArgs = @(
    "-Wno-dev",
    "-A", $CMakeArch,
    ".."
)

# Add vcpkg if available
if ($env:VCPKG_ROOT) {
    $cmakeArgs += "-DVCPKG_ROOT=$env:VCPKG_ROOT"
    Write-Host "  Using VCPKG_ROOT: $env:VCPKG_ROOT" -ForegroundColor Cyan
}

& $cmakePath $cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed"
    exit 1
}

# Build the project
Write-Host "Building PHD2 in Release configuration..." -ForegroundColor Yellow
& $cmakePath --build . --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    exit 1
}

# Run tests. A test failure aborts the installer build - matches the
# build-deb.sh (dh_auto_test) and build-dmg.sh (set -e + ctest) gates.
# CTest is shipped with CMake so it should always be on PATH; the absolute-
# path fallback covers a CMake install that didn't update PATH.
Write-Host "Running tests..." -ForegroundColor Yellow
# Get-Command returns a CommandInfo on success, not a path string. Pull
# .Source so subsequent Test-Path / display interpolation see a real path
# instead of just "ctest.exe".
$ctestCmd = Get-Command ctest -ErrorAction SilentlyContinue
if ($ctestCmd) {
    $ctestPath = $ctestCmd.Source
} else {
    # ctest ships next to cmake, so look in the directory we resolved cmake
    # from (covers the VS-bundled CMake, which is never on PATH).
    $ctestPath = Join-Path (Split-Path -Parent $cmakePath) "ctest.exe"
    if (-not (Test-Path $ctestPath)) {
        $ctestPath = "C:\Program Files\CMake\bin\ctest.exe"
    }
}
if (Test-Path $ctestPath) {
    & $ctestPath --build-config Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed - aborting installer build. Fix the failing tests, or reconfigure with -DPHD_BUILD_TESTS=OFF to drop the test build entirely."
        exit 1
    }
} else {
    Write-Error "CTest not found at '$ctestPath' and not on PATH. Cannot verify build."
    exit 1
}

# Generate installer script from template
Write-Host "Generating installer script..." -ForegroundColor Yellow
$issTemplate = Join-Path $RootDir $InstallerTemplate
$issContent = Get-Content $issTemplate -Raw
$issContent = $issContent -replace '@VERSION@', $fullVersion
$issOutput = Join-Path (Get-Location) "phd2.iss"
# Use UTF-8 without BOM (InnoSetup doesn't like BOM)
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($issOutput, $issContent, $utf8NoBom)

# Generate README from template
Write-Host "Generating README..." -ForegroundColor Yellow
$readmeTemplate = Join-Path $RootDir "README-PHD2.txt.in"
if (Test-Path $readmeTemplate) {
    $readmeContent = Get-Content $readmeTemplate -Raw
    $readmeContent = $readmeContent -replace '@VERSION@', $fullVersion
    $readmeOutput = Join-Path (Get-Location) "README-PHD2.txt"
    # Use UTF-8 without BOM
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($readmeOutput, $readmeContent, $utf8NoBom)
}

# Create installer. Output filename uses the openastro-phd2-<version>-<arch>
# convention shared with the macOS .dmg and Linux .deb produced by the
# sibling build scripts. Inno Setup's OutputBaseFilename default in
# phd2.iss.in is overridden via the /F flag here.
Write-Host "Creating installer..." -ForegroundColor Yellow
$archSuffix = $InstallerArch.TrimStart("-")  # "-x64" -> "x64"
$installerName = "openastro-phd2-$fullVersion-$archSuffix"
& $isccPath $issOutput "/F$installerName"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Installer creation failed"
    exit 1
}

# Check if installer was created
$installerPath = Join-Path (Get-Location) "$installerName.exe"
if (Test-Path $installerPath) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Installer created successfully!" -ForegroundColor Green
    Write-Host "Location: $installerPath" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    # Get file size
    $fileInfo = Get-Item $installerPath
    $fileSizeMB = [math]::Round($fileInfo.Length / 1MB, 2)
    Write-Host "Installer size: $fileSizeMB MB" -ForegroundColor Cyan
} else {
    Write-Error "Installer file not found at expected location: $installerPath"
    exit 1
}
