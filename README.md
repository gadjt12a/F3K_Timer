# F3K Timer

A hand-held F3K (discus-launched glider) competition timer for a **caller** — the pilot's field assistant who coaches the pilot in real time.

## Features

- Working time countdown with visual arc indicator
- Flight timer (start/stop with button A)
- Flight log showing best times
- Audio alerts at key time thresholds (30s, 15s, 10-1s countdown)
- Configurable working time (1-15 minutes)

## Hardware

**Target device:** Waveshare ESP32-S3-Touch-AMOLED-1.75C
- 466x466 round AMOLED display
- ESP32-S3 dual-core
- ES8311 audio codec
- AXP2101 power management

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
| A (GPIO16) | Green | Start/stop flight, confirm |
| B (GPIO17) | Blue | Scratch flight, settings adjust |

- **Short press A** from idle: Start working time + flight
- **Hold A** from idle: Enter settings
- **Short press A** during flight: Stop and record flight
- **Short press B** during working time: Scratch last flight
- **Hold B**: Abort round

## Building for Hardware

```
pio run -e waveshare
pio run -e waveshare --target upload
```

## License

MIT
