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

# Check for required tools
Write-Host "Checking for required tools..." -ForegroundColor Yellow

# Check CMake
$cmakePath = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakePath) {
    $cmakePath = "C:\Program Files\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmakePath)) {
        Write-Error "CMake not found. Please install CMake and ensure it's in your PATH, or install it to the default location."
        exit 1
    }
}
Write-Host "  CMake: $cmakePath" -ForegroundColor Green

# Check InnoSetup. Prefer Inno Setup 6 (current release) over the legacy 5,
# and look under both the 64-bit and 32-bit Program Files trees.
$isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
    "C:\Program Files\Inno Setup 5\ISCC.exe"
)
$isccPath = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $isccPath) {
    Write-Error "InnoSetup not found. Please install Inno Setup 6 (preferred) or 5 to a default location."
    Write-Host "  Searched:" -ForegroundColor Yellow
    foreach ($c in $isccCandidates) { Write-Host "    $c" -ForegroundColor Yellow }
    exit 1
}
Write-Host "  InnoSetup: $isccPath" -ForegroundColor Green

# Check WXWIN. thirdparty.cmake hard-fails the configure step unless WXWIN
# points at a wxWidgets install with a static vc_x64_lib build. Validate it
# here so the failure is an actionable message up front rather than a CMake
# error deep in the configure.
if (-not $env:WXWIN) {
    Write-Error "WXWIN is not set. Point it at your wxWidgets install (a static x64 build)."
    Write-Host "  Expected layout:" -ForegroundColor Yellow
    Write-Host "    %WXWIN%\include\wx\..." -ForegroundColor Yellow
    Write-Host "    %WXWIN%\lib\vc_x64_lib\   (static Release/Debug libs)" -ForegroundColor Yellow
    exit 1
}
if (-not (Test-Path $env:WXWIN -PathType Container)) {
    Write-Error "WXWIN is set to '$env:WXWIN' but that is not an existing directory."
    exit 1
}
$wxLibDir = Join-Path $env:WXWIN "lib\vc_x64_lib"
if (-not (Test-Path $wxLibDir -PathType Container)) {
    Write-Error "WXWIN='$env:WXWIN' does not contain the expected static x64 libs at 'lib\vc_x64_lib'."
    Write-Host "  Build wxWidgets 3.2 as a static x64 library (vc_x64_lib), or fix WXWIN." -ForegroundColor Yellow
    exit 1
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
    $ctestPath = "C:\Program Files\CMake\bin\ctest.exe"
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
