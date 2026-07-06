# F3K Timer

A hand-held F3K (discus-launched glider) competition timer for a **caller** — the pilot's field assistant who coaches the pilot in real time.

## Features

- Working time countdown with visual arc indicator
- All times displayed with hundredths of a second (MM:SS.CC)
- Flight timer (start/stop with button)
- Flight log showing top 3 best times with traffic light colors:
  - 1st best = Green
  - 2nd best = Orange
  - 3rd best = Yellow
  - Scratched = Red (strikethrough)
- Audio alerts at key time thresholds (30s, 15s, 10-1s countdown)
- Configurable working time (1-15 minutes)
- Battery indicator
- **Base station WiFi connectivity** — connects to F3K_BASE AP, receives TASK/START/STOP/PILOTS commands, reports flights back
- **Pilot selection UI** — driven by PILOTS command from base station
- **Connection indicator** — idle screen shows BASE... (grey) while connecting, BASE OK (green) when live

## Hardware

**Target device:** Waveshare ESP32-S3-Touch-AMOLED-1.75C
- 466x466 round AMOLED display
- ESP32-S3 dual-core
- ES8311 audio codec
- AXP2101 power management

## Base Station

The timer connects as a WiFi client to the F3K base station AP (`F3K_BASE`). The base station is a Raspberry Pi 4 running `server.py` on port 8765. See the `F3K_Timer_Project` repo for the base station setup.

Network credentials are hardcoded in `include/config.h` — the timer AP is a closed, dedicated network.

## Simulation (Wokwi)

You can run this project in simulation without the physical hardware.

### Prerequisites

1. [VS Code](https://code.visualstudio.com/)
2. [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
3. [Wokwi extension](https://docs.wokwi.com/vscode/getting-started) (free trial available)

### Running the Simulation

1. Clone this repo:
   ```
   git clone https://github.com/gadjt12a/F3K_Timer.git
   ```
2. Open the folder in VS Code
3. Wait for PlatformIO to install dependencies (automatic, may take a minute)
4. Build the project:
   - `Ctrl+Shift+P` → "PlatformIO: Build"
   - Select the `wokwi` environment
5. Start the simulator:
   - `Ctrl+Shift+P` → "Wokwi: Start Simulator"

### Simulation Controls

| Button | Color | Action |
|--------|-------|--------|
| R (GPIO17) | Blue | Primary: Start/stop flight |
| L (GPIO16) | Green | Secondary: Scratch, WT only start |

**From Idle:**
- **R click**: Start working time + flight together
- **L click**: Start working time only (wait for launch)
- **R hold**: Enter settings

**During Round:**
- **R click**: Start/stop flight
- **L click**: Scratch last recorded flight
- **R very long hold (2s)**: Abort round

**Settings:**
- **R click**: +1 minute
- **L click**: -1 minute
- **R hold** or timeout: Confirm and exit

## Building for Hardware

On Windows, `pio` is not on PATH — use the full PlatformIO path:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare --target upload --project-dir "C:\Kris\Projects\F3K_Timer_1"
```

## License

MIT
