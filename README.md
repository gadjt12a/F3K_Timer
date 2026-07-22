# F3K Timer

A hand-held competition timer for a **caller** — the pilot's field assistant who coaches the pilot in real time during F3K (discus-launched glider) and F5K rounds.

## Features

- Working time countdown with colour-coded arc (green → orange → red)
- All times displayed with hundredths of a second (MM:SS.CC)
- Flight timer — start/stop with a single button press
- Flight log showing best times with traffic-light ranking:
  - 1st best = Green
  - 2nd best = Orange
  - 3rd best = Yellow
  - Scratched = Red (strikethrough)
- Audio alerts at key time thresholds (30s, 15s, 10–1s countdown)
- Configurable working time (1–15 minutes via settings)
- **F3K / F5K task selection** — choose task type in settings; F5K enables post-round altitude entry
- **Altitude entry (F5K)** — after time expires, enter launch altitude per flight with rollover dials:
  - R = +1 m (ones digit, rolls 0 → 9 → 0)
  - L = +10 m (tens digit, rolls 0 → 10 → … → 100 → 0)
  - R hold = confirm altitude and advance to next flight
- Battery indicator (% + charging state)
- **Base station WiFi connectivity** — connects to F3K_BASE AP, receives TASK/START/STOP/PILOTS/COUNT commands, reports FLIGHT/ALTITUDE/SELECT back; queues messages when disconnected and flushes on reconnect
- **Pilot selection UI** — scrollable list driven by PILOTS command from base station; SELECT sent on confirm
- **10-second countdown arc** — green sweep during pre-round countdown from base
- **Timer ID display** — after ASSIGN, shows `T1` / `T2` etc. bold green on idle screen between battery indicator and GLIDE title
- **Connection indicator** — idle screen shows BASE… (grey) while connecting, BASE OK (green) when live; BASE OK replaced by pilot name once a pilot is selected
- **NVS round history (ROUND RECALL)** — stores last 3 rounds (discipline, pilot name, flight times, F5K altitudes) to ESP32 NVS; each flight written immediately so data survives power loss mid-round; accessible via `STATE_HISTORY` from the expired screen (L) or settings chain; "N of 3" slot indicator, L=older / R=newer/exit, 8s inactivity timeout
- **Pilot decouple** — timer clears pilot binding automatically when returning to idle after a completed round
- **OTA firmware updates** — settings page 3 checks for updates from the base station HTTP server; R hold applies the update; device reboots automatically when done

## Hardware

**Target device:** Waveshare ESP32-S3-Touch-AMOLED-1.75C (SKU 33691)

| Component | Detail |
|-----------|--------|
| SoC | ESP32-S3R8, dual-core LX7 @ 240 MHz |
| Display | 1.75" AMOLED round, 466×466, CO5300 driver |
| Audio | ES8311 codec via I2S |
| Power management | AXP2101 PMIC (also provides L button via power-key IRQ) |
| Battery | 3.7 V Li-Ion, MX1.25 connector |

### Button layout (stopwatch orientation — buttons at 12 o'clock)

| Physical button | Position | Role |
|----------------|----------|------|
| PWR (AXP2101) | Top-left | **L** — secondary: start WT only, scratch, settings adjust |
| BOOT (GPIO0) | Top-right | **R** — primary: start/stop flight, confirm, navigate |

> Do not hold R (BOOT) while powering on — GPIO0 held LOW at boot forces download mode.

## State Machine

```
IDLE
  R hold       → SETTINGS
  R click      → FLIGHT_RUNNING  (starts WT + flight together)
  L click      → WORKING_TIME_RUNNING  (WT only — wait for launch)

WORKING_TIME_RUNNING
  R click      → FLIGHT_RUNNING
  L click      → SCRATCH_CONFIRM  (if flights recorded)
  R hold 2s   → WORKING_TIME_EXPIRED  (abort)
  WT expires  → WORKING_TIME_EXPIRED

FLIGHT_RUNNING
  R click      → WORKING_TIME_RUNNING  (stop & record flight)
  R hold 2s   → WORKING_TIME_EXPIRED  (abort, discard flight)
  WT expires  → WORKING_TIME_EXPIRED  (auto-record)

SCRATCH_CONFIRM
  R click      → WORKING_TIME_RUNNING  (confirmed)
  2s timeout  → WORKING_TIME_RUNNING  (cancelled)

WORKING_TIME_EXPIRED
  R click      → ALTITUDE_ENTRY  (F5K, if flights exist)
               → IDLE  (F3K, clears pilot binding)
  L click      → HISTORY  (browse last round in NVS)

ALTITUDE_ENTRY  (F5K only)
  R click      → +1 m  (ones digit, 0→9→0)
  L click      → +10 m  (tens digit, 0→100→0)
  R hold       → confirm altitude, next flight (or IDLE when done; IDLE clears pilot binding)

HISTORY  (NVS round recall — up to 3 slots)
  L click      → older slot (slot+1, max slot 2)
  R click      → newer slot (slot-1), or IDLE when at slot 0
  R hold       → IDLE
  8s timeout  → IDLE

SETTINGS  (page 1: working time)
  R click      → +1 minute
  L click      → −1 minute
  R hold / 8s timeout → TASK_SELECT

TASK_SELECT  (page 2: task type)
  R or L click → toggle F3K / F5K
  R hold / 3s timeout → OTA_CHECK

OTA_CHECK  (page 3: firmware update)
  on entry   → async version check to base station :8080
  R hold     → download + flash update (when available)
  R click / 8s timeout → IDLE

PILOT_SELECT  (base station only)
  R click      → next pilot
  L click      → previous pilot
  R hold       → confirm, → IDLE

COUNTDOWN  (base station COUNT 10…1)
  COUNT N      → display arc + beep
  START        → WORKING_TIME_RUNNING + long tone
```

## Base Station

The timer connects as a WiFi client to the F3K base station AP (`F3K_BASE`). The base station is a Raspberry Pi running `server.py` on port 8765. See the `F3K_Timer_Project` repo for setup.

Network credentials are hardcoded in `include/config.h` — the timer AP is a closed, dedicated network.

## Simulation (Wokwi)

Run without physical hardware using the Wokwi VS Code extension.

### Prerequisites

1. [VS Code](https://code.visualstudio.com/)
2. [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
3. [Wokwi extension](https://docs.wokwi.com/vscode/getting-started)

### Running

1. Clone this repo and open in VS Code
2. Wait for PlatformIO to install dependencies
3. `Ctrl+Shift+P` → "PlatformIO: Build" → select `wokwi` environment
4. `Ctrl+Shift+P` → "Wokwi: Start Simulator"

Buttons in sim: GPIO16 (green) = L, GPIO17 (blue) = R.

## Building for Hardware

```powershell
# Build and flash
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare --target upload --upload-port COM4 --project-dir "C:\Kris\Projects\F3K_Timer_1"

# Serial monitor
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --environment waveshare --baud 115200 --project-dir "C:\Kris\Projects\F3K_Timer_1"
```

Flash mode: hold BOOT, tap RESET, release BOOT — device enumerates as USB serial on COM4.

## Firmware Releases

Compiled firmware snapshots are stored in `firmware/releases/` and tagged in git as `fw-vN`.
The last 5 builds are kept on disk; all git tags are kept indefinitely.

### Create a release

```powershell
.\scripts\release-firmware.ps1
```

Builds the waveshare firmware with the correct `FW_VERSION` string embedded, copies binaries
to `firmware/releases/fw-vN/`, updates `firmware/ota/` for base station serving, commits,
tags `fw-vN`, then prints the push command. Run at the end of each session before pushing.

> Do not use `-SkipBuild` for real releases — the embedded version string must match the tag
> or the OTA check will loop.

### Roll back

To revert the device to a previous build, flash the binaries directly from the release folder:

```powershell
esptool.py --chip esp32s3 --port COM4 --baud 921600 write_flash `
    0x00000 firmware\releases\fw-v9\bootloader.bin `
    0x08000 firmware\releases\fw-v9\partitions.bin `
    0x10000 firmware\releases\fw-v9\firmware.bin
```

Flash addresses and the source commit hash are recorded in each `release.txt`.
To inspect or rebuild from an older source state: `git checkout fw-vN`.

## Project Structure

```
include/
  config.h          task types, timing constants, AppState + OtaStatus enums
  fw_version.h      auto-generated by release script; defines FW_VERSION "fw-vN"
  pin_config.h      all GPIO defines
src/
  main.cpp          setup(), loop(), state machine
  timer/            WorkingTime, FlightTimer, FlightLog, RoundHistory (NVS)
  display/          UI (round AMOLED + Wokwi sim paths), ArcRenderer
  input/            Buttons (AXP2101 PWR key + GPIO0 BOOT)
  audio/            Tones (I2S sine wave alerts)
  ota/              OtaUpdater — async HTTPUpdate + version check (hardware only)
  comms/            TimerComms — WiFi TCP client; pending ring buffer for offline messages
firmware/
  releases/         last 5 compiled builds (fw-vN/firmware.bin + .bin files)
  ota/              current OTA files served by base station HTTP server
```

## License

MIT
