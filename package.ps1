# Package AnimSpeedFix for the Centurion launcher's optional-patch system.
#
# Produces, in dist\:
#   stealth-glide.zip       extracted into the client root by the launcher
#   stealth-glide.version   plain-text version string the launcher polls
#
# Upload BOTH to the launcher update URL (settings.json launcherUpdateUrl,
# default https://centurionpvp.com/downloads/). The launcher fetches
# "<name>.version" to decide whether to (re)download "<name>.zip".
#
# Only dinput8.dll ships. AnimSpeedFix.ini is deliberately NOT included: the
# defaults are compiled in, so shipping it would clobber any local edits on
# every patch update. Power users can drop their own ini alongside and the
# launcher will leave it alone.

param(
    [string]$Name = 'client-tweaks',   # was 'stealth-glide' before it grew
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$out  = Join-Path $root 'out'
$dist = Join-Path $root 'dist'

$dll = Join-Path $out 'dinput8.dll'
if (-not (Test-Path $dll)) {
    Write-Error "dinput8.dll not found in out\ - run build.bat first."
}

# Version format matches the launcher's existing patches: 1.00000, 1.00022, ...
if (-not $Version) {
    $verFile = Join-Path $dist "$Name.version"
    if (Test-Path $verFile) {
        $prev = (Get-Content $verFile -Raw).Trim()
        if ($prev -match '^(\d+)\.(\d+)$') {
            $Version = '{0}.{1:D5}' -f $Matches[1], ([int]$Matches[2] + 1)
        }
    }
    if (-not $Version) { $Version = '1.00000' }
}

# the launcher rejects these characters in a version string
if ($Version -match '[/\\<>:"|?*]') { Write-Error "Invalid version string: $Version" }

New-Item -ItemType Directory -Force -Path $dist | Out-Null
$stage = Join-Path $dist '_stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# zip contents are extracted relative to the client root (extractPath: '.'),
# so dinput8.dll must sit at the archive root, not in a subfolder.
Copy-Item $dll $stage

$zip = Join-Path $dist "$Name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

# .version must be the bare string, no trailing newline surprises (launcher trims)
[System.IO.File]::WriteAllText((Join-Path $dist "$Name.version"), $Version)

Write-Host ""
Write-Host "Packaged $Name @ $Version" -ForegroundColor Green
Get-ChildItem $dist -File | Where-Object { $_.Name -like "$Name.*" } |
    Select-Object Name, Length | Format-Table -AutoSize

Write-Host "Archive contents:"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
$archive.Entries | ForEach-Object { "  {0,-24} {1,9} bytes" -f $_.FullName, $_.Length }
$archive.Dispose()

Write-Host ""
Write-Host "Upload both files to the launcher update URL, then ship a launcher"
Write-Host "build whose FileMap contains a '$Name' entry."
