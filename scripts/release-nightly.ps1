[CmdletBinding()]
param([switch]$AllowDirty)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$constantsPath = Join-Path $ProjectRoot 'src\core\constants.h'
$match = Select-String -Path $constantsPath -Pattern 'RE7HT_VERSION\s*=\s*"([^"]+)"'
if (-not $match) {
    throw "Could not extract RE7HT_VERSION from $constantsPath"
}
$version = $match.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'resident-evil-7' `
    -ModName 'RE7HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
