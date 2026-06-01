#!/usr/bin/env pwsh
#Requires -Version 5.1
# Automated release workflow: bump version, generate changelog, commit, tag,
# push. Push triggers .github/workflows/release.yml on the upstream repo.
#
# Headless: zero stdin reads. The deterministic safety gates (clean tree,
# tag absent, semver valid, on main) ARE the confirmation.

param(
    [Parameter(Position = 0)]
    [string]$Version = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir   = Split-Path -Parent $scriptDir
$manifestPath = Join-Path $projectDir 'manifest.json'

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1'
if (-not (Test-Path $module)) {
    throw "ReleaseWorkflow.psm1 not found at $module. Run 'pixi run sync' first."
}
Import-Module $module -Force

function Get-CurrentVersion {
    $json = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
    return $json.version
}

function Set-CurrentVersion {
    param([Parameter(Mandatory = $true)][string]$NewVersion)
    $json = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
    $json.version = $NewVersion
    $json | ConvertTo-Json -Depth 10 | Set-Content $manifestPath -NoNewline
}

Write-Host "=== RE7 Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: $currentVersion" -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor Yellow
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

# Step 1: validate / resolve semver
$Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
$tagName = "v$Version"

# Step 2: branch / clean tree / tag preconditions
$currentBranch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($currentBranch -ne 'main') {
    throw "Must be on 'main' branch (currently on '$currentBranch')."
}

$status = git status --porcelain
if ($status) {
    throw "Working tree has uncommitted changes. Commit or stash first."
}

$existingTag = git tag -l $tagName
if ($existingTag) {
    throw "Tag '$tagName' already exists. Pick a new version."
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Step 3: bump canonical version source
Write-Host "Updating manifest.json -> $Version..." -ForegroundColor Cyan
Set-CurrentVersion $Version

# manifest.json is canonical. The runtime version constant, CMake project
# version, and install.cmd MOD_VERSION are mirrors kept in lockstep so the
# built DLL, the release ZIP name, and a later nightly all agree.
$installCmdPath = Join-Path $scriptDir 'install.cmd'
if (Test-Path $installCmdPath) {
    (Get-Content -Raw -Path $installCmdPath) `
        -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" |
        Set-Content $installCmdPath -NoNewline
}

$constantsPath = Join-Path $projectDir 'src/core/constants.h'
if (Test-Path $constantsPath) {
    (Get-Content -Raw -Path $constantsPath) `
        -replace '(RE7HT_VERSION\s*=\s*")[^"]*(")', "`${1}$Version`${2}" |
        Set-Content $constantsPath -NoNewline
}

$cmakePath = Join-Path $projectDir 'CMakeLists.txt'
if (Test-Path $cmakePath) {
    (Get-Content -Raw -Path $cmakePath) `
        -replace '(project\(RE7HeadTracking VERSION )\d+\.\d+\.\d+', "`${1}$Version" |
        Set-Content $cmakePath -NoNewline
}

# Step 4: build (release config) - abort on failure
Write-Host "Running 'pixi run build-release'..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed (exit $LASTEXITCODE). Aborting release."
}

# Step 5: regenerate CHANGELOG from commits since last tag
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$changelogPath  = Join-Path $projectDir 'CHANGELOG.md'
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    Set-Content $changelogPath "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
} else {
    $changelogArgs = @{
        ChangelogPath = $changelogPath
        Version       = $Version
        ArtifactPaths = @('src/', 'cameraunlock-core/', 'scripts/install.cmd', 'scripts/uninstall.cmd')
    }
    New-ChangelogFromCommits @changelogArgs
}

# Step 6: commit version + changelog
Write-Host "Committing 'Release v$Version'..." -ForegroundColor Cyan
git add $manifestPath $changelogPath $installCmdPath $constantsPath $cmakePath
git commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) {
    throw "git commit failed (exit $LASTEXITCODE)."
}

# Step 7: annotated tag
Write-Host "Creating annotated tag $tagName..." -ForegroundColor Cyan
git tag -a $tagName -m "Release v$Version"
if ($LASTEXITCODE -ne 0) {
    throw "git tag failed (exit $LASTEXITCODE)."
}

# Step 8: push commits + tag (non-force; never overwrite remote)
Write-Host "Pushing commits + tag to origin..." -ForegroundColor Cyan
git push origin main
if ($LASTEXITCODE -ne 0) {
    throw "git push origin main failed (exit $LASTEXITCODE)."
}
git push origin $tagName
if ($LASTEXITCODE -ne 0) {
    throw "git push origin $tagName failed (exit $LASTEXITCODE)."
}

Write-Host ""
Write-Host "Release $tagName initiated. CI will build and publish the GitHub Release." -ForegroundColor Green
