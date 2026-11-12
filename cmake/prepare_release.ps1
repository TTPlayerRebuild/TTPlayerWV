[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Version, [string]$PreviousVersion,
      [Parameter(Mandatory=$true)][string]$Repository, [Parameter(Mandatory=$true)][string]$Commit,
      [string]$ArtifactDirectory = 'artifact', [string]$ServerUrl = 'https://github.com')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$Version = (Get-PluginBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-PluginVersionTag $PreviousVersion)) { throw 'Invalid previous version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'Invalid release identity.' }
$project = Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'CMakeLists.txt') -Raw
$plugin = if ($project -match 'project\(TtpWv ') { 'ttp_wv' } elseif ($project -match 'project\(TtpOfr ') { 'ttp_ofr' } else { throw 'Unknown plugin project.' }
$names = @("$plugin-$Version.zip", "$plugin-$Version-source.zip")
$expected = @($names | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $_) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $_"
}) -join "`n"
$manifest = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim() -replace "`r", ''
if ($manifest -cne $expected) { throw 'Archive SHA256/version manifest mismatch.' }
$url = "$ServerUrl/$Repository"
$notes = "# $Version`n`nSource: $url/tree/$Commit`n`nMSVC Win32 Release; XP/Win7 imports audited. Runtime ZIP contains AddIn DLL and checksums. Source attachment includes build scripts, SDK headers, dependency sources or the original OptimFROG release archive, and licenses. Import audits do not certify playback on an untested OS.`n"
if ($PreviousVersion) { $notes += "`nChanges: $url/compare/$PreviousVersion...$Version`n" }
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8
