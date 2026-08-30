#!/usr/bin/env pwsh
#Requires -Version 5.1
# Automated release workflow: bump version, generate changelog, commit, tag,
# push. Push triggers .github/workflows/release.yml on the upstream repo.
#
# Headless: zero stdin reads. The deterministic safety gates (clean tree,
# tag absent, semver valid, on main) ARE the confirmation.

param(
    [Parameter(Position = 0)]
    [string]$Version = "",
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
$noticesRoot = Split-Path -Parent $PSScriptRoot
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

$scriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir   = Split-Path -Parent $scriptDir
$manifestPath = Join-Path $projectDir 'manifest.json'

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1'
if (-not (Test-Path $module)) {
    throw "ReleaseWorkflow.psm1 not found at $module. Run 'pixi run sync' first."
}
Import-Module $module -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

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

# Step 3: regenerate CHANGELOG from commits since last tag. This is the gate
# that aborts when there are no user-facing commits, so run it BEFORE mutating
# any version files or building - a failure here then leaves a clean tree
# instead of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$changelogPath  = Join-Path $projectDir 'CHANGELOG.md'
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    Set-Content $changelogPath "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $Version
            ArtifactPaths = @('src/', 'cameraunlock-core/', 'scripts/install.cmd', 'scripts/uninstall.cmd')
        }
        New-ChangelogFromCommits @changelogArgs
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

# Step 4: bump canonical version source
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

# Step 5: build (release config) - abort on failure
Write-Host "Running 'pixi run build-release'..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed (exit $LASTEXITCODE). Aborting release."
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
