#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package the RE7 Head Tracking installer + nexus ZIPs into release/.
# Consumes whatever is committed under vendor/reframework/ - bump the
# vendored REFramework via `pixi run update-deps` before tagging a
# release. This script never reaches out to the network.

Set-StrictMode -Version Latest
$ErrorActionPreference  = "Stop"
$ProgressPreference     = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

# Canonical version source: manifest.json
$manifestPath = Join-Path $projectDir 'manifest.json'
if (-not (Test-Path $manifestPath)) {
    throw "manifest.json not found at $manifestPath - cannot determine release version."
}
$manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
$version  = $manifest.version
if (-not $version) {
    throw "manifest.json at $manifestPath has no .version field."
}

$modName = 'RE7HeadTracking'

Write-Host "=== $modName - Package Release ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# Required source artifacts (build must have already run)
$dllPath = Join-Path $projectDir "bin/Release/$modName.dll"
if (-not (Test-Path $dllPath)) {
    throw "$modName.dll not found at: $dllPath. Run 'pixi run build-release' first."
}

$iniPath = Join-Path $projectDir 'HeadTracking.ini'
if (-not (Test-Path $iniPath)) {
    throw "HeadTracking.ini not found at: $iniPath"
}

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($cmdScript in @('install.cmd', 'uninstall.cmd')) {
    $cmdPath = Join-Path $scriptsDir $cmdScript
    if (-not (Test-Path $cmdPath)) {
        throw "Required script not found: $cmdPath"
    }
}

$vendorDir = Join-Path $projectDir 'vendor/reframework'
$vendorZip = Join-Path $vendorDir 'RE7.zip'
if (-not (Test-Path $vendorZip)) {
    throw "Vendored REFramework not found at: $vendorZip. Run 'pixi run update-deps' and commit, then retry."
}

# --- Installer ZIP (GitHub Releases) ---
Write-Host "--- Installer ZIP ---" -ForegroundColor Yellow
Write-Host ""

$ghStagingDir = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStagingDir) { Remove-Item -Recurse -Force $ghStagingDir }
New-Item -ItemType Directory -Path $ghStagingDir -Force | Out-Null

foreach ($cmdScript in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $cmdScript) -Destination $ghStagingDir -Force
    Write-Host "  $cmdScript" -ForegroundColor Green
}

# install.cmd / uninstall.cmd resolve the game via shared/find-game.ps1.
# Bundle that shim alongside them so the release ZIP is self-contained.
Copy-SharedBundle -StagingDir $ghStagingDir

$pluginsDir = Join-Path $ghStagingDir 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $dllPath -Destination $pluginsDir -Force
Write-Host "  plugins/$modName.dll" -ForegroundColor Green
Copy-Item $iniPath -Destination $pluginsDir -Force
Write-Host "  plugins/HeadTracking.ini" -ForegroundColor Green

# Vendor tree: install.cmd extracts vendor/reframework/RE7.zip at user-install
# time, so the loader zip + LICENSE + README must travel inside the installer ZIP.
$ghVendorDir = Join-Path $ghStagingDir 'vendor/reframework'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
# All three are mandatory. The loader binary is redistributed here, so its
# licence and provenance record are not optional extras that may be silently
# skipped - a guarded copy would turn a licence violation into a green build.
$vendorAssets = @('RE7.zip', 'LICENSE', 'README.md')
foreach ($asset in $vendorAssets) {
    $src = Join-Path $vendorDir $asset
    if (-not (Test-Path $src)) {
        throw "vendor/reframework/$asset is missing. The installer ZIP redistributes the REFramework binary and must carry its licence and provenance record alongside it."
    }
    Copy-Item $src -Destination $ghVendorDir -Force
    Write-Host "  vendor/reframework/$asset" -ForegroundColor Green
}

# launcher-manifest.json is the file lopari reads at the installer-ZIP root.
# Stamp the real release version in so mod_info.version never drifts from
# manifest.json. (Not staged into the Nexus ZIP - Nexus users do not use the
# launcher.)
$launcherManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}
$launcherManifest = Get-Content -Raw -Path $launcherManifestPath | ConvertFrom-Json
$launcherManifest.mod_info.version = $version
$stagedManifestPath = Join-Path $ghStagingDir 'launcher-manifest.json'
$manifestJson = $launcherManifest | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($stagedManifestPath, $manifestJson, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "  launcher-manifest.json (v$version)" -ForegroundColor Green

$docFiles = @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')
foreach ($doc in $docFiles) {
    $docPath = Join-Path $projectDir $doc
    if (-not (Test-Path $docPath)) {
        throw "Required notice file not found: $doc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $docPath -Destination $ghStagingDir -Force
    Write-Host "  $doc" -ForegroundColor Green
}

$ghZipName = "$modName-v$version-installer.zip"
$ghZipPath = Join-Path $releaseDir $ghZipName
if (Test-Path $ghZipPath) { Remove-Item $ghZipPath -Force }

Write-Host ""
Write-Host "Creating installer ZIP..." -ForegroundColor Cyan
Push-Location $ghStagingDir
try {
    Compress-Archive -Path '.\*' -DestinationPath $ghZipPath -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $ghStagingDir

$ghZipSize = (Get-Item $ghZipPath).Length / 1KB
Write-Host ("  $ghZipPath ({0:N1} KB)" -f $ghZipSize) -ForegroundColor Green

# --- Nexus ZIP (extract-to-game-folder) ---
Write-Host ""
Write-Host "--- Nexus ZIP ---" -ForegroundColor Yellow
Write-Host ""

$nexusStagingDir = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStagingDir) { Remove-Item -Recurse -Force $nexusStagingDir }

$nexusPluginsDir = Join-Path $nexusStagingDir 'reframework\plugins'
New-Item -ItemType Directory -Path $nexusPluginsDir -Force | Out-Null

Copy-Item $dllPath -Destination $nexusPluginsDir -Force
Write-Host "  reframework/plugins/$modName.dll" -ForegroundColor Green
Copy-Item $iniPath -Destination $nexusPluginsDir -Force
Write-Host "  reframework/plugins/HeadTracking.ini" -ForegroundColor Green

$nexusZipName = "$modName-v$version-nexus.zip"
$nexusZipPath = Join-Path $releaseDir $nexusZipName
if (Test-Path $nexusZipPath) { Remove-Item $nexusZipPath -Force }

Write-Host ""
Write-Host "Creating Nexus ZIP..." -ForegroundColor Cyan
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStagingDir -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Push-Location $nexusStagingDir
try {
    Compress-Archive -Path '.\*' -DestinationPath $nexusZipPath -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $nexusStagingDir

$nexusZipSize = (Get-Item $nexusZipPath).Length / 1KB
Write-Host ("  $nexusZipPath ({0:N1} KB)" -f $nexusZipSize) -ForegroundColor Green

# --- Summary ---
Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Host ""
Write-Host ("Installer:  $ghZipPath ({0:N1} KB)"    -f $ghZipSize)    -ForegroundColor Green
Write-Host ("Nexus Mods: $nexusZipPath ({0:N1} KB)" -f $nexusZipSize) -ForegroundColor Green

Write-Output $ghZipPath
Write-Output $nexusZipPath
