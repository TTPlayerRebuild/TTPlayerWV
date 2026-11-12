[CmdletBinding()]
param([string]$BuildDirectory, [string]$Generator = 'Visual Studio 18 2026',
      [string[]]$CMakeArguments = @(), [switch]$Package, [switch]$SourcePackage, [string]$PackageVersion)
$ErrorActionPreference = 'Stop'
$plugin = 'ttp_wv'
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build' }
. (Join-Path $PSScriptRoot 'cmake/version.ps1')
$PackageVersion = (Get-PluginBuildVersion $PackageVersion).Name
function Invoke-CMake([string[]]$Arguments) {
    $previous = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & cmake @Arguments 2>&1 | ForEach-Object { $_.ToString() }; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "CMake failed ($code): $Arguments" }
}
Invoke-CMake (@('-S',$PSScriptRoot,'-B',$BuildDirectory,'-G',$Generator,'-A','Win32',"-DTTP_BUILD_VERSION=$PackageVersion") + $CMakeArguments)
Invoke-CMake @('--build',$BuildDirectory,'--config','Release','--target',$plugin,'--parallel','4')
$output = Join-Path $BuildDirectory 'Release'
Assert-PluginFileVersion (Join-Path $output "$plugin.dll") $PackageVersion
if (-not ($Package -or $SourcePackage)) { return }
$stage = Join-Path $BuildDirectory ('package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Invoke-CMake @('--install',$BuildDirectory,'--config','Release','--component','Runtime','--prefix',$stage)
$hash = (Get-FileHash -LiteralPath (Join-Path $stage "AddIn/$plugin.dll") -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  AddIn/$plugin.dll" | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding UTF8
$archive = Join-Path $output "$plugin-$PackageVersion.zip"
Compress-Archive -LiteralPath (Join-Path $stage 'AddIn'),(Join-Path $stage 'SHA256SUMS.txt') -DestinationPath $archive -Force
$archives = @($archive)
if ($SourcePackage) {
    & python (Join-Path $PSScriptRoot 'cmake/package_source.py') --root $PSScriptRoot --build $BuildDirectory --version $PackageVersion
    if ($LASTEXITCODE -ne 0) { throw 'Source packaging failed.' }
    $archives += Join-Path $output "$plugin-$PackageVersion-source.zip"
}
Get-FileHash -LiteralPath $archives -Algorithm SHA256 |
    ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(),[IO.Path]::GetFileName($_.Path) } |
    Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding UTF8
Write-Output "Packages: $output"
