# F3K Timer -- New Device Flash Tool
# Erases and flashes the latest firmware release to a Waveshare ESP32-S3 device.
# Use for brand-new devices or when switching partition tables.
#
# Usage:
#   .\tools\flash-new-device.ps1                  # erase + flash, auto COM port
#   .\tools\flash-new-device.ps1 -Port COM5        # specify port
#   .\tools\flash-new-device.ps1 -EraseOnly        # chip erase only
#   .\tools\flash-new-device.ps1 -FlashOnly        # skip erase, flash only
#   .\tools\flash-new-device.ps1 -Version fw-v10   # flash a specific release

param(
    [string]$Port    = "",
    [string]$Version = "",
    [switch]$EraseOnly,
    [switch]$FlashOnly
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

$Python  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$Esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"

if (-not (Test-Path $Python)) {
    Write-Host "ERROR: PlatformIO Python not found at:"
    Write-Host "  $Python"
    Write-Host "Install PlatformIO via VS Code or 'pip install platformio' first."
    exit 1
}
if (-not (Test-Path $Esptool)) {
    Write-Host "ERROR: esptool not found at:"
    Write-Host "  $Esptool"
    Write-Host "Run a PlatformIO build for the waveshare env first to install tool packages."
    exit 1
}

# ---------------------------------------------------------------------------
# Find firmware release
# ---------------------------------------------------------------------------

$ScriptDir   = Split-Path $MyInvocation.MyCommand.Path -Parent
$ReleasesDir = Join-Path $ScriptDir "..\firmware\releases"

if ($Version -ne "") {
    $FwDir = Join-Path $ReleasesDir $Version
    if (-not (Test-Path $FwDir)) {
        Write-Host "ERROR: Release '$Version' not found in firmware\releases\"
        Write-Host "Available releases:"
        Get-ChildItem $ReleasesDir -Directory | Where-Object { $_.Name -match "^fw-v\d+$" } |
            Sort-Object { [int]($_.Name -replace "fw-v","") } |
            ForEach-Object { Write-Host "  $($_.Name)" }
        exit 1
    }
} else {
    $FwDir = Get-ChildItem $ReleasesDir -Directory |
        Where-Object { $_.Name -match "^fw-v\d+$" } |
        Sort-Object { [int]($_.Name -replace "fw-v","") } |
        Select-Object -Last 1 -ExpandProperty FullName

    if (-not $FwDir) {
        Write-Host "ERROR: No firmware releases found in firmware\releases\"
        Write-Host "Run the /release-firmware skill first."
        exit 1
    }
    $Version = Split-Path $FwDir -Leaf
}

$Bootloader  = Join-Path $FwDir "bootloader.bin"
$Partitions  = Join-Path $FwDir "partitions.bin"
$Firmware    = Join-Path $FwDir "firmware.bin"

foreach ($f in @($Bootloader, $Partitions, $Firmware)) {
    if (-not (Test-Path $f)) {
        Write-Host "ERROR: Missing file: $f"
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Auto-detect COM port if not specified
# ---------------------------------------------------------------------------

if ($Port -eq "") {
    $EspPorts = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match "CP210|CH340|CH341|FTDI|USB Serial|USB-SERIAL" }
    if ($EspPorts.Count -eq 1) {
        $Port = ($EspPorts[0].FriendlyName -replace ".*\((COM\d+)\).*",'$1')
        Write-Host "Auto-detected port: $Port ($($EspPorts[0].FriendlyName))"
    } elseif ($EspPorts.Count -gt 1) {
        Write-Host "Multiple serial devices found -- specify -Port:"
        $EspPorts | ForEach-Object {
            $p = ($_.FriendlyName -replace ".*\((COM\d+)\).*",'$1')
            Write-Host "  $p  $($_.FriendlyName)"
        }
        exit 1
    } else {
        $Port = "COM4"
        Write-Host "No serial device auto-detected, defaulting to COM4."
        Write-Host "If that is wrong, rerun with -Port COMx"
    }
}

# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "F3K Timer -- New Device Flash Tool"
Write-Host "-----------------------------------"
Write-Host "Firmware : $Version"
Write-Host "  $Bootloader"
Write-Host "  $Partitions"
Write-Host "  $Firmware"
Write-Host "Port     : $Port"
if ($EraseOnly) { Write-Host "Mode     : ERASE ONLY" }
elseif ($FlashOnly) { Write-Host "Mode     : FLASH ONLY (no erase)" }
else { Write-Host "Mode     : ERASE + FLASH (recommended for new devices)" }
Write-Host ""

# ---------------------------------------------------------------------------
# Device setup instructions
# ---------------------------------------------------------------------------

Write-Host "DEVICE SETUP -- put device in DOWNLOAD MODE:"
Write-Host "  1. Hold the BOOT button (top-right, labelled R)"
Write-Host "  2. Tap RESET (or connect the USB cable while holding BOOT)"
Write-Host "  3. Release BOOT"
Write-Host "  Device will appear as $Port in Device Manager."
Write-Host ""
Write-Host "Press ENTER when the device is in download mode (Ctrl+C to cancel)..."
Read-Host | Out-Null

# ---------------------------------------------------------------------------
# Step 1 -- Erase
# ---------------------------------------------------------------------------

if (-not $FlashOnly) {
    Write-Host ""
    Write-Host "[1/2] Erasing flash -- approx 30 seconds..."
    Write-Host ""
    & $Python $Esptool --chip esp32s3 --port $Port erase_flash
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "ERASE FAILED (exit $LASTEXITCODE)"
        Write-Host "Check: device in download mode? Correct port? USB cable data-capable?"
        exit 1
    }
    Write-Host ""
    Write-Host "Erase complete."
}

# ---------------------------------------------------------------------------
# Step 2 -- Flash
# ---------------------------------------------------------------------------

if (-not $EraseOnly) {
    Write-Host ""
    Write-Host "[2/2] Flashing $Version -- approx 60 seconds..."
    Write-Host ""
    & $Python $Esptool --chip esp32s3 --port $Port --baud 921600 write_flash `
        0x00000 $Bootloader `
        0x08000 $Partitions `
        0x10000 $Firmware
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "FLASH FAILED (exit $LASTEXITCODE)"
        Write-Host "Check: device still in download mode? Try reducing baud: add -Baud 460800"
        exit 1
    }
    Write-Host ""
    Write-Host "Flash complete."
}

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "-----------------------------------"
if ($EraseOnly) {
    Write-Host "Chip erased. Device is blank -- flash firmware before use."
} else {
    Write-Host "Device is ready."
    Write-Host "Unplug and reconnect (or short-press PWR) to boot into $Version."
    if (Test-Path (Join-Path $FwDir "release.txt")) {
        Write-Host ""
        Write-Host "Release notes:"
        Get-Content (Join-Path $FwDir "release.txt") | ForEach-Object { Write-Host "  $_" }
    }
}
Write-Host ""
