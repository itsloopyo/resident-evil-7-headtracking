#!/usr/bin/env pwsh
#Requires -Version 5.1
# Resolve the Resident Evil 7 install path via cameraunlock-core's
# canonical detection module (env var > Steam appmanifest > Steam folder >
# GOG > Epic > Xbox). Single source of truth lives in
# cameraunlock-core/data/games.json under the `resident-evil-7` key.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1'
if (-not (Test-Path $module)) {
    throw "GamePathDetection.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$gameId   = 'resident-evil-7'
$gamePath = Find-GamePath -GameId $gameId

if ($gamePath) {
    Write-Output $gamePath
    exit 0
}

$config = Get-GameConfig -GameId $gameId
Write-GameNotFoundError `
    -GameName    $config.DisplayName `
    -EnvVar      $config.EnvVar `
    -SteamFolder $config.SteamFolder
exit 1
