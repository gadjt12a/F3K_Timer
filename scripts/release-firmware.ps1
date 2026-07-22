# release-firmware.ps1
# Snapshot the waveshare firmware as a numbered release, commit, and tag.
#
# Usage:
#   .\scripts\release-firmware.ps1            # build then snapshot
#   .\scripts\release-firmware.ps1 -SkipBuild # snapshot from last .pio build

param([switch]$SkipBuild)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ReleasesDir = Join-Path $ProjectRoot "firmware\releases"
$BuildDir    = Join-Path $ProjectRoot ".pio\build\waveshare"

# 1. Auto-increment version from existing fw-vN tags
$lastN = 0
foreach ($tag in (git -C $ProjectRoot tag --list "fw-v*" 2>$null)) {
    if ($tag -match '^fw-v(\d+)$') {
        $n = [int]$Matches[1]
        if ($n -gt $lastN) { $lastN = $n }
    }
}
$ver = "fw-v" + ($lastN + 1)
Write-Host "==> Version: $ver"

# 2. Build
if (-not $SkipBuild) {
    Write-Host "==> Building waveshare firmware..."
    Push-Location $ProjectRoot
    & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare
    $exit = $LASTEXITCODE
    Pop-Location
    if ($exit -ne 0) { Write-Error "Build failed."; exit 1 }
} else {
    Write-Host "==> Skipping build (using existing .pio output)"
}

# 3. Verify artifacts
$required = "firmware.bin", "bootloader.bin", "partitions.bin"
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $BuildDir $f))) {
        Write-Error "Missing $f - build first or remove -SkipBuild."; exit 1
    }
}

# 4. Create release folder and copy binaries
$releaseDir = Join-Path $ReleasesDir $ver
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
foreach ($f in $required) {
    Copy-Item (Join-Path $BuildDir $f) $releaseDir
}
Write-Host "==> Binaries copied to $releaseDir"

# 5. Write release metadata
$hash = (git -C $ProjectRoot rev-parse HEAD 2>$null).Trim()
$date = Get-Date -Format "yyyy-MM-dd HH:mm"
$info = "version : $ver", "date    : $date", "commit  : $hash", "",
        "Flash (esptool):", "  --chip esp32s3 --baud 921600 write_flash",
        "  0x00000 bootloader.bin  0x08000 partitions.bin  0x10000 firmware.bin"
Set-Content -Path (Join-Path $releaseDir "release.txt") -Value $info

# 6. Trim to 5 most recent releases
$all = Get-ChildItem $ReleasesDir -Directory |
       Where-Object { $_.Name -match '^fw-v\d+$' } |
       Sort-Object { [int]($_.Name -replace 'fw-v', '') }
if ($all.Count -gt 5) {
    foreach ($d in ($all | Select-Object -First ($all.Count - 5))) {
        Remove-Item $d.FullName -Recurse -Force
        Write-Host "==> Removed old release: $($d.Name)"
    }
}

# 7. Commit and tag
git -C $ProjectRoot add "firmware/"
git -C $ProjectRoot commit -m "Release $ver -- compiled waveshare firmware snapshot ($hash)"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

git -C $ProjectRoot tag $ver
Write-Host "==> Tagged $ver"
Write-Host ""
Write-Host "Done. To push:  git push; git push origin $ver"
