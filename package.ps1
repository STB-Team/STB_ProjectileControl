# Assemble the installable archive.
#
#   .\package.ps1                       # -> package/STB Projectile Control.7z
#   .\package.ps1 -NoArchive            # leave the staged tree, don't compress
#
# No FOMOD here: one DLL covers SE and AE and there is nothing for the player to choose,
# so the archive is a plain mod folder any manager drops in as is.

[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [switch]$NoArchive
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$stage = Join-Path $root 'package/stage'
$dll = Join-Path $root "build/$Configuration/STB_ProjectileControl.dll"

if (-not (Test-Path -LiteralPath $dll)) {
    throw "No plugin at $dll -- build it first (cmake --build build --config $Configuration)."
}

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $stage 'SKSE/Plugins') -Force | Out-Null

Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'SKSE/Plugins')
Copy-Item -Path (Join-Path $root 'dist/SKSE/Plugins/*.ini') -Destination (Join-Path $stage 'SKSE/Plugins')

# Strings for the HUD hint. No keycap art ships with the mod: the hint borrows whatever
# the player's interface already provides (SkyUI is a requirement, and its buttonart.swf
# exports the clip we attach), so there is nobody's artwork to redistribute.
New-Item -ItemType Directory -Path (Join-Path $stage 'Interface') -Force | Out-Null
Copy-Item -Path (Join-Path $root 'dist/Interface/*') -Destination (Join-Path $stage 'Interface')

Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage

Write-Host "Staged:" -ForegroundColor Cyan
Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    '  {0}' -f $_.FullName.Substring($stage.Length + 1)
}

if ($NoArchive) { return }

$sevenZip = Get-Command '7z.exe' -ErrorAction SilentlyContinue
if (-not $sevenZip) {
    # No 7-Zip: a plain zip, which every mod manager and Nexus take just as well.
    $zip = Join-Path $root 'package/STB Projectile Control.zip'
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "7z.exe not on PATH -- wrote $zip instead" -ForegroundColor Green
    return
}
$archive = Join-Path $root 'package/STB Projectile Control.7z'
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
& $sevenZip.Source a -t7z -mx=9 $archive (Join-Path $stage '*') | Out-Null
Write-Host "Wrote $archive" -ForegroundColor Green
