#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh the vendored REFramework nightly to the latest upstream. The
# vendored zip is the install-time source of truth, so this is a manual
# bump-and-commit step. Build/package/CI never call out to the network -
# they consume whatever is committed under vendor/reframework/.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

# Praydog ships a single universal REFramework.zip (dinput8.dll +
# reframework_revision.txt) that works across all supported RE Engine games,
# including RE7. We pin the on-disk filename to RE7.zip so deploy.ps1,
# package-release.ps1 and install.cmd can hardcode it.
$out = Join-Path $projectDir 'vendor/reframework'
Update-VendoredLoader `
    -Name 'reframework' `
    -OutputDir $out `
    -OutputFileName 'RE7.zip' `
    -Owner 'praydog' -Repo 'REFramework-nightly' `
    -AssetPattern '^REFramework\.zip$' `
    -AllowPrerelease `
    -LicenseUrl 'https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE' | Out-Null

# Update-VendoredLoader records the commit it resolved the release tag to. For
# REFramework that tag lives in praydog/REFramework-nightly, a separate
# publishing repo whose commit hashes have nothing to do with REFramework's
# source history, so quoting it as "the REFramework commit" is a false
# provenance claim. Relabel it and record the revision the archive states about
# itself, which is what THIRD-PARTY-NOTICES.md attributes.
$vendorZip  = Join-Path $out 'RE7.zip'
$readmePath = Join-Path $out 'README.md'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($vendorZip)
try {
    $revEntry = $archive.Entries | Where-Object { $_.FullName -eq 'reframework_revision.txt' }
    if (-not $revEntry) {
        throw "$vendorZip has no reframework_revision.txt. The upstream archive shape changed, so the REFramework revision can no longer be read from it. Re-derive it by hand rather than shipping an unverified attribution."
    }
    $reader = New-Object System.IO.StreamReader($revEntry.Open())
    try { $sourceRevision = $reader.ReadToEnd().Trim() } finally { $reader.Dispose() }
} finally {
    $archive.Dispose()
}

if ($sourceRevision -notmatch '^[0-9a-f]{40}$') {
    throw "reframework_revision.txt did not contain a commit SHA (got '$sourceRevision')."
}

$provenanceTemplate = @'
- Stored as: `RE7.zip` (the upstream asset is renamed on download so install.cmd,
  deploy.ps1 and package-release.ps1 can hardcode one filename)
- REFramework source revision: `__SOURCE_REV__`
  (read from `reframework_revision.txt` inside the archive; this is the
  authoritative revision to attribute)
- Publisher repo commit: `__PUBLISHER_SHA__`
  (a commit in praydog/REFramework-nightly, the repository the nightly builds
  are published from. It is NOT a REFramework source commit and must not be
  quoted as one.)
'@

$readme = Get-Content -Raw -Path $readmePath
$commitMatch = [regex]::Match($readme, '- Commit: `([0-9a-f]{40})`')
if (-not $commitMatch.Success) {
    throw "Could not find the '- Commit: <sha>' line in $readmePath to relabel. Update-VendoredLoader's README format changed - re-check the provenance wording by hand before committing."
}
$provenance = $provenanceTemplate.TrimEnd() `
    -replace '__SOURCE_REV__', $sourceRevision `
    -replace '__PUBLISHER_SHA__', $commitMatch.Groups[1].Value
$readme = $readme.Remove($commitMatch.Index, $commitMatch.Length).Insert($commitMatch.Index, $provenance)
Set-Content -Path $readmePath -Value $readme -Encoding UTF8 -NoNewline

# The plugin SDK headers under extern/reframework/ are a second, separate copy
# of praydog's code, and THIRD-PARTY-NOTICES.md states they are byte-identical
# to the vendored revision. Bumping the loader past a revision where the API
# changed would silently make that statement false, so verify it here.
foreach ($header in @('API.h', 'API.hpp')) {
    $localPath   = Join-Path $projectDir "extern/reframework/$header"
    $upstreamUrl = "https://raw.githubusercontent.com/praydog/REFramework/$sourceRevision/include/reframework/$header"
    $upstream    = (Invoke-WebRequest -Uri $upstreamUrl -UseBasicParsing).Content
    if ($upstream -is [byte[]]) { $upstream = [System.Text.Encoding]::UTF8.GetString($upstream) }
    $local = Get-Content -Raw -Path $localPath
    $normalize = { param($s) ($s -replace "`r`n", "`n").TrimEnd() }
    if ((& $normalize $upstream) -ne (& $normalize $local)) {
        throw "extern/reframework/$header no longer matches upstream at $sourceRevision. THIRD-PARTY-NOTICES.md claims both SDK headers are byte-identical to the vendored revision, and that claim is now false. Refresh the headers from $upstreamUrl and update extern/reframework/README.md before committing this bump."
    }
    Write-Host "  extern/reframework/$header matches upstream at $sourceRevision" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "vendor/reframework refreshed (REFramework revision $sourceRevision). Review and commit." -ForegroundColor Green
