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

# Check prerequisites
Write-Status "Checking prerequisites..."

$missing = @()
if (-not (Test-Command "git")) { $missing += "git" }
if (-not (Test-Command "cmake")) { $missing += "cmake" }

if ($missing.Count -gt 0) {
    Write-Host "Missing required tools: $($missing -join ', ')" -ForegroundColor Red
    Write-Host "Please install them and try again."
    Write-Host "  - Git: https://git-scm.com/download/win"
    Write-Host "  - CMake: https://cmake.org/download/"
    exit 1
}

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
