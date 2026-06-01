#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy a local build into the resolved RE7 install. REFramework is sourced
# from the committed vendor/reframework/RE7.zip - bump it via
# `pixi run update-deps` and commit. No network access at deploy time.

param(
    [Parameter(Position = 0)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Write-Host "Deploying $Configuration build to Resident Evil 7..." -ForegroundColor Cyan

# Resolve game path via the canonical detection module
$gamePath = & (Join-Path $scriptDir 'detect-game.ps1')
if ($LASTEXITCODE -ne 0 -or -not $gamePath) {
    exit 1
}
$gamePath = ($gamePath | Select-Object -Last 1).ToString().Trim()
Write-Host "Game directory: $gamePath" -ForegroundColor Gray

# Install REFramework from vendored copy if not already present
$reframeworkDll = Join-Path $gamePath 'dinput8.dll'
if (-not (Test-Path $reframeworkDll)) {
    $vendorZip = Join-Path $projectDir 'vendor/reframework/RE7.zip'
    if (-not (Test-Path $vendorZip)) {
        throw "REFramework not installed and vendored copy missing at $vendorZip. Run 'pixi run update-deps', commit, then retry."
    }
    Write-Host "REFramework not found. Extracting bundled copy..." -ForegroundColor Yellow
    Expand-Archive -Path $vendorZip -DestinationPath $gamePath -Force
    if (-not (Test-Path $reframeworkDll)) {
        throw "REFramework install failed: dinput8.dll not found after extraction."
    }
    Write-Host "  REFramework installed from vendor/reframework/RE7.zip." -ForegroundColor Green
} else {
    Write-Host "REFramework present, skipping loader install." -ForegroundColor Gray
}

# Ensure plugins directory exists
$pluginsDir = Join-Path $gamePath 'reframework\plugins'
if (-not (Test-Path $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
    Write-Host "  Created: reframework/plugins/" -ForegroundColor Green
}

$sourceDll = Join-Path $projectDir "bin\$Configuration\RE7HeadTracking.dll"
$sourceIni = Join-Path $projectDir 'HeadTracking.ini'

if (-not (Test-Path $sourceDll)) {
    throw "Build artifact not found: $sourceDll. Run 'pixi run build' (or 'build-release') first."
}

$targetDll = Join-Path $pluginsDir 'RE7HeadTracking.dll'
$targetIni = Join-Path $pluginsDir 'HeadTracking.ini'

if (Test-Path $targetDll) {
    Copy-Item $targetDll "$targetDll.bak" -Force
    Write-Host "  Backed up existing DLL" -ForegroundColor Gray
}

Copy-Item $sourceDll $targetDll -Force
Write-Host "  Copied: RE7HeadTracking.dll" -ForegroundColor Green

# INI is config; only seed if missing so user edits survive redeploy
if (-not (Test-Path $targetIni)) {
    if (Test-Path $sourceIni) {
        Copy-Item $sourceIni $targetIni -Force
        Write-Host "  Copied: HeadTracking.ini (default config)" -ForegroundColor Green
    }
} else {
    Write-Host "  Skipped: HeadTracking.ini (preserving existing config)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Deployment complete." -ForegroundColor Green
Write-Host "Files deployed to: $pluginsDir" -ForegroundColor Cyan
