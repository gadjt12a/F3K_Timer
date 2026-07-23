# F3K Timer — Device Flash Tool

Use this tool to flash a brand-new Waveshare ESP32-S3 device, or to recover a device
that will not boot. It erases the chip and writes the latest firmware in one step.

---

## Prerequisites

- Windows 10 or 11
- [PlatformIO](https://platformio.org/install/ide?install=vscode) installed via VS Code
- A USB cable that carries data (not charge-only)
- The F3K Timer repo cloned locally (you already have this if you are reading this file)

Run a PlatformIO build for the `waveshare` environment at least once before using this
tool — that installs the esptool package that the script depends on.

---

## Step 1 — Put the device in download mode

The device must be in download mode before the script can talk to it.

1. **Hold** the BOOT button — top-right button when the screen faces you (labelled R)
2. **Tap** RESET, or plug in the USB cable while still holding BOOT
3. **Release** BOOT

The device will not show anything on screen in download mode — that is normal.
It should appear as a COM port in Device Manager (Ports section).

> Do not hold BOOT while powering on normally — GPIO0 held LOW at boot forces
> download mode and the device will not start the timer app.

---

## Step 2 — Run the flash tool

Open a PowerShell terminal in the project root and run:

```powershell
.\tools\flash-new-device.ps1
```

The script will:
1. Find the latest firmware release automatically
2. Auto-detect the COM port (or default to COM4)
3. Prompt you to confirm the device is in download mode
4. Erase the chip (~30 seconds)
5. Flash bootloader, partition table, and firmware (~60 seconds)
6. Report success and print the release notes

### If the device is on a port other than COM4

```powershell
.\tools\flash-new-device.ps1 -Port COM5
```

### Flash a specific older release

```powershell
.\tools\flash-new-device.ps1 -Version fw-v10
```

### Erase only (without flashing)

```powershell
.\tools\flash-new-device.ps1 -EraseOnly
```

---

## Step 3 — Boot the device

Once the script reports success:

- Unplug and reconnect the USB cable, **or**
- Short-press the PWR button (top-left)

The timer app will start. On first boot after a full erase, NVS storage is blank —
working time will default to 7 minutes and task type to F3K.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Port not found` or `Failed to connect` | Device not in download mode — repeat Step 1 |
| Multiple COM ports detected, script exits | Unplug other serial devices and rerun, or use `-Port COMx` |
| `esptool not found` | Run a PlatformIO waveshare build first to install tool packages |
| Erase succeeds but flash fails | Try a lower baud rate: add `--baud 460800` inside the script |
| Script runs but device does not boot after | Repeat the full erase + flash — do not use `-FlashOnly` on a blank chip |

---

## Notes on the partition table

fw-v11 introduced a dual-OTA partition layout (`partitions_f3k_ota.csv`). The chip
erase is required because the factory partition table and the F3K partition table occupy
the same flash region — writing the new table on top of the old one without erasing can
leave stale OTA boot data that causes a bootloop.

Once a device is running fw-v11 or later, future updates can be applied via OTA
(Settings → Task Type → OTA Check on the device) without a USB cable or chip erase.
