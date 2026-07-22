# release-firmware.ps1
# Build and snapshot the waveshare firmware.
#
# Usage:
#   .\scripts\release-firmware.ps1            # auto-increment version
#   .\scripts\release-firmware.ps1 -SkipBuild # use last .pio build output as-is
#
# What it does:
#   1. Determines the next fw-vN version from existing git tags
#   2. Builds the waveshare env (unless -SkipBuild)
#   3. Copies firmware.bin / bootloader.bin / partitions.bin to firmware/releases/fw-vN/
#   4. Writes a flash.ps1 convenience script and release.txt metadata inside the folder
#   5. Trims firmware/releases/ to the 5 most recent folders (oldest deleted)
#   6. Stages firmware/, commits, then tags that commit fw-vN
#   7. Prints the push command — you decide when to push

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ReleasesDir = Join-Path $ProjectRoot "firmware\releases"
$BuildDir    = Join-Path $ProjectRoot ".pio\build\waveshare"

# ── 1. Determine next version ─────────────────────────────────────────────────
$existingTags = git -C $ProjectRoot tag --list "fw-v*" 2>$null
$lastN = 0
foreach ($tag in $existingTags) {
    if ($tag -match '^fw-v(\d+)$') {
        $n = [int]$Matches[1]
        if ($n -gt $lastN) { $lastN = $n }
    }
}
$nextN      = $lastN + 1
$versionStr = "fw-v$nextN"
Write-Host "==> Version: $versionStr"

# ── 2. Build ──────────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "==> Building waveshare firmware..."
    Push-Location $ProjectRoot
    pio run -e waveshare
    $exitCode = $LASTEXITCODE
    Pop-Location
    if ($exitCode -ne 0) {
        Write-Error "Build failed — aborting release."
        exit 1
    }
} else {
    Write-Host "==> Skipping build (using existing .pio output)"
}

# Verify build artifacts exist
foreach ($f in @("firmware.bin","bootloader.bin","partitions.bin")) {
    $p = Join-Path $BuildDir $f
    if (-not (Test-Path $p)) {
        Write-Error "Missing $f in $BuildDir — run without -SkipBuild or build first."
        exit 1
    }
}

# ── 3. Create release folder and copy binaries ────────────────────────────────
$releaseDir = Join-Path $ReleasesDir $versionStr
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

Copy-Item (Join-Path $BuildDir "firmware.bin")    $releaseDir
Copy-Item (Join-Path $BuildDir "bootloader.bin")  $releaseDir
Copy-Item (Join-Path $BuildDir "partitions.bin")  $releaseDir
Write-Host "==> Binaries copied to $releaseDir"

# ── 4. Write flash.ps1 and release.txt ───────────────────────────────────────
$commitHash = git -C $ProjectRoot rev-parse HEAD 2>$null
$releaseDate = Get-Date -Format "yyyy-MM-dd HH:mm"

$flashContent = @"
# Flash $versionStr to a connected Waveshare ESP32-S3-Touch-AMOLED-1.75C
#
# Before running: put device in download mode
#   Hold BOOT (R button), tap RESET/Power-on, release BOOT
#
# Run this script from the folder it lives in:
#   cd firmware\releases\$versionStr
#   .\flash.ps1 [-Port COM3]
#
param([string]`$Port = "")

`$esptool = "esptool.py"

if (`$Port -eq "") {
    # Try to auto-detect — picks the first available ESP port
    `$found = & `$esptool flash_id 2>&1 | Select-String "Serial port"
    if (`$found) {
        `$Port = (`$found.ToString() -split " ")[-1].Trim()
        Write-Host "Detected port: `$Port"
    } else {
        Write-Error "No device detected. Specify -Port COM3 (or whichever port the device appears on)."
        exit 1
    }
}

Write-Host "Flashing $versionStr to `$Port ..."
& `$esptool --chip esp32s3 --port `$Port --baud 921600 write_flash ``
    0x0     bootloader.bin ``
    0x8000  partitions.bin ``
    0x10000 firmware.bin

if (`$LASTEXITCODE -eq 0) {
    Write-Host "Done. Press RESET to boot."
} else {
    Write-Error "Flash failed."
}
"@

Set-Content -Path (Join-Path $releaseDir "flash.ps1")    -Value $flashContent
Set-Content -Path (Join-Path $releaseDir "release.txt")  -Value @"
version : $versionStr
date    : $releaseDate
commit  : $commitHash
env     : waveshare (ESP32-S3-Touch-AMOLED-1.75C)

Flash addresses:
  0x00000  bootloader.bin
  0x08000  partitions.bin
  0x10000  firmware.bin
"@

# ── 5. Trim to 5 most recent releases ────────────────────────────────────────
$maxKeep = 5
$allReleases = Get-ChildItem $ReleasesDir -Directory |
               Where-Object { $_.Name -match '^fw-v\d+$' } |
               Sort-Object { [int]($_.Name -replace 'fw-v','') }

if ($allReleases.Count -gt $maxKeep) {
    $excess = $allReleases.Count - $maxKeep
    $toRemove = $allReleases | Select-Object -First $excess
    foreach ($dir in $toRemove) {
        Remove-Item $dir.FullName -Recurse -Force
        Write-Host "==> Removed old release: $($dir.Name)"
    }
}

# ── 6. Commit then tag ────────────────────────────────────────────────────────
git -C $ProjectRoot add "firmware/"
git -C $ProjectRoot commit -m "Release $versionStr — compiled waveshare firmware snapshot

Built from commit $commitHash"

if ($LASTEXITCODE -ne 0) {
    Write-Error "git commit failed — check status."
    exit 1
}

git -C $ProjectRoot tag $versionStr
Write-Host "==> Tagged $versionStr"

# ── 7. Done ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Release $versionStr is committed and tagged."
Write-Host "To push:  git push && git push origin $versionStr"
Write-Host "To flash: cd firmware\releases\$versionStr && .\flash.ps1"
