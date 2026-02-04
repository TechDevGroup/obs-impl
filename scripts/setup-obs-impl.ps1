# OBS-Impl Setup Script
# Pulls the source and builds the multi-output stages version of OBS

param(
    [string]$InstallDir = "$env:USERPROFILE\obs-impl",
    [switch]$BuildOnly,
    [switch]$RunAfterBuild,
    [string]$BuildType = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$RepoUrl = "https://github.com/TechDevGroup/obs-impl.git"

function Write-Status {
    param([string]$Message)
    Write-Host "`n[OBS-Impl] $Message" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Command)
    return [bool](Get-Command $Command -ErrorAction SilentlyContinue)
}

# Check prerequisites and auto-install if possible
Write-Status "Checking prerequisites..."

function Install-WithWinget {
    param([string]$PackageId, [string]$Name)

    if (-not (Test-Command "winget")) {
        Write-Host "winget not available. Please install $Name manually." -ForegroundColor Red
        return $false
    }

    Write-Host "Installing $Name via winget..." -ForegroundColor Yellow
    winget install --id $PackageId --accept-source-agreements --accept-package-agreements

    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    return $true
}

if (-not (Test-Command "git")) {
    Write-Host "Git not found." -ForegroundColor Yellow
    if (-not (Install-WithWinget "Git.Git" "Git")) {
        Write-Host "  Install from: https://git-scm.com/download/win" -ForegroundColor Red
        exit 1
    }
}

if (-not (Test-Command "cmake")) {
    Write-Host "CMake not found." -ForegroundColor Yellow
    if (-not (Install-WithWinget "Kitware.CMake" "CMake")) {
        Write-Host "  Install from: https://cmake.org/download/" -ForegroundColor Red
        exit 1
    }
}

# Verify after install attempts
$missing = @()
if (-not (Test-Command "git")) { $missing += "git" }
if (-not (Test-Command "cmake")) { $missing += "cmake" }

if ($missing.Count -gt 0) {
    Write-Host "Still missing: $($missing -join ', ')" -ForegroundColor Red
    Write-Host "Please restart your terminal after installation and try again."
    exit 1
}

# Check for Visual Studio 2022
function Test-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        return [bool]$vsPath
    }
    # Fallback: check common paths
    $paths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) { return $true }
    }
    return $false
}

if (-not (Test-VisualStudio)) {
    Write-Host "Visual Studio 2022 with C++ tools not found." -ForegroundColor Yellow
    if (Test-Command "winget") {
        Write-Host "Installing Visual Studio 2022 Build Tools via winget..." -ForegroundColor Yellow
        Write-Host "This may take a while..." -ForegroundColor Yellow
        winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements --accept-package-agreements --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621"

        if (-not (Test-VisualStudio)) {
            Write-Host "Visual Studio Build Tools installation may require a restart." -ForegroundColor Yellow
            Write-Host "Please restart your computer and run this script again." -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "Please install Visual Studio 2022 with C++ desktop development workload:" -ForegroundColor Red
        Write-Host "  https://visualstudio.microsoft.com/downloads/" -ForegroundColor Red
        exit 1
    }
}

Write-Host "All prerequisites found." -ForegroundColor Green

# Clone or update repository
if (-not $BuildOnly) {
    if (Test-Path $InstallDir) {
        Write-Status "Updating existing repository..."
        Push-Location $InstallDir
        git fetch origin
        git pull origin master
        Pop-Location
    } else {
        Write-Status "Cloning repository to $InstallDir..."
        git clone $RepoUrl $InstallDir
    }
}

Push-Location $InstallDir

# Initialize submodules
Write-Status "Initializing submodules..."
git submodule update --init --recursive

# Create build directory
$BuildDir = Join-Path $InstallDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configure with CMake
Write-Status "Configuring build with CMake..."
Push-Location $BuildDir

# Use CMake presets if available, otherwise manual config
if (Test-Path (Join-Path $InstallDir "CMakePresets.json")) {
    cmake --preset windows-x64 ..
} else {
    cmake -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_BUILD_TYPE=$BuildType `
        ..
}

# Build
Write-Status "Building OBS-Impl ($BuildType)..."
cmake --build . --config $BuildType --parallel

Pop-Location

# Output location
$OutputDir = Join-Path $BuildDir "rundir\$BuildType"
$ObsExe = Join-Path $OutputDir "bin\64bit\obs64.exe"

Write-Status "Build complete!"
Write-Host "Output directory: $OutputDir" -ForegroundColor Green

if (Test-Path $ObsExe) {
    Write-Host "Executable: $ObsExe" -ForegroundColor Green

    if ($RunAfterBuild) {
        Write-Status "Launching OBS-Impl..."
        Start-Process $ObsExe
    } else {
        Write-Host "`nTo launch, run:" -ForegroundColor Yellow
        Write-Host "  & `"$ObsExe`""
    }
} else {
    Write-Host "Note: obs64.exe not found at expected location." -ForegroundColor Yellow
    Write-Host "Check $OutputDir for build output."
}

Pop-Location

Write-Host "`n[OBS-Impl] Setup complete!" -ForegroundColor Green
