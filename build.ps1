#requires -Version 5.1
<#
.SYNOPSIS
    Configure and build juce-arranger.

.DESCRIPTION
    Wraps CMake (Visual Studio 2022 generator). Configures the build tree the
    first time, then builds. Pass -Clean to wipe the build tree and reconfigure.

.PARAMETER Config
    Build configuration: Debug or Release. Default: Debug.

.PARAMETER Clean
    Delete the build directory before configuring.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Release
    .\build.ps1 -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root     = $PSScriptRoot
$buildDir = Join-Path $root 'cmake-build'

# Verify CMake is available.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake or open a 'Developer PowerShell for VS 2022'."
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir ..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# Configure if the build tree doesn't exist yet.
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "Configuring (Visual Studio 17 2022) ..." -ForegroundColor Cyan
    cmake -S $root -B $buildDir -G "Visual Studio 17 2022"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }
}

Write-Host "Building ($Config) ..." -ForegroundColor Cyan
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }

Write-Host "Build succeeded." -ForegroundColor Green
$exe = Join-Path $root 'juce-arranger.exe'
if (Test-Path $exe) {
    Write-Host "Executable: $exe" -ForegroundColor Green
}
