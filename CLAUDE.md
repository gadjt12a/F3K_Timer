# F3K Timer — Claude Code Project Reference

> **Read this file fully before making any change to this project.**
> It is the authoritative reference for hardware, libraries, build environments,
> and coding constraints. When in doubt, consult this file first.

---

## Project Purpose

A hand-held F3K (discus-launched glider) competition timer for a **caller** — the pilot's
field assistant. The caller stands on the field, watches the sky, and coaches the pilot
in real time: calling out working time remaining, confirming flight times recorded, flagging
what's happening in the air, and advising on task strategy.

The device is **not** for the pilot — it is for the person supporting them.

### What the caller needs from the device

- **Working time countdown** — primary information, must dominate the display
- **Current flight time** — visible at a glance while a flight is in progress
- **Flight log for the current task** — which flights have been recorded, which is the
  best, how many more are needed to complete the task
- **Audio alerts** — the caller's eyes are on the sky; sound is the primary notification
  channel for time thresholds
- **One-handed operation** — device in one hand, possibly signalling the pilot with the other
- **Eyes-off readability** — when the caller does look down, key info must be readable
  instantly without hunting around the screen

### What the caller does NOT need

- GPS, compass, altimeter — irrelevant to the caller's role
- Complex menus during a round — keep settings access minimal during flight
- Anything that requires two hands or sustained screen attention

Long-term: ESP-NOW mesh with a base station for multi-pilot score aggregation.

---

## Target Hardware

**Waveshare ESP32-S3-Touch-AMOLED-1.75C** (SKU 33691 — with battery)

| Component         | Detail                                              |
|-------------------|-----------------------------------------------------|
| SoC               | ESP32-S3R8 — dual-core LX7 @ 240MHz                |
| Flash / PSRAM     | 16MB Flash, 8MB PSRAM                               |
| Display           | 1.75" AMOLED round, 466×466, CO5300 driver (QSPI)  |
| Touch             | CST9217 capacitive — I2C 0x5A                      |
| IMU               | QMI8658 6-axis — I2C 0x6B                           |
| RTC               | PCF85063 — I2C 0x51                                 |
| Audio codec       | ES8311 — I2C 0x18, I2S output                      |
| Microphones       | ES7210 dual-mic array — I2C (not used in this app) |
| Power management  | AXP2101 — I2C 0x34                                  |
| Vibration motor   | **NOT PRESENT** — no haptic feedback                |
| Battery           | 3.7V Li-Ion, MX1.25 2-pin connector                 |

### Physical button layout and display orientation

The PWR button (AXP2101) and BOOT button (GPIO0) are both on the same physical edge of
the module. By applying `setRotation(1)` (90° clockwise) in display init, both buttons
sit at **12 o'clock** relative to the displayed content — i.e. stopwatch-style, exactly
as a timekeeper would expect.

**This means both physical buttons are used for in-flight input on real hardware:**

| Physical button | GPIO / source | Code name | Role |
|-----------------|---------------|-----------|------|
| BOOT (top-right when rotated) | GPIO0 | `btnB` / **R** | **Primary** — start/stop flight, confirm scratch, navigate settings |
| PWR (top-left when rotated) | AXP2101 power-key IRQ | `btnA` / **L** | **Secondary** — start WT only, trigger scratch, adjust settings |

> In `main.cpp` these are aliased as `btnR` (BOOT) and `btnL` (PWR) to reflect their physical position after rotation. The `Buttons` API still calls them A and B internally.

> **GPIO0 strapping pin note:** GPIO0 held LOW at power-on forces download mode.
> Enable it as an input only after `setup()` has run — the bootloader window closes
> before user code starts. A brief press during normal operation is completely safe.
> Document for users: do not hold R (BOOT) while pressing Reset/Power-on.

Touch gestures (CST9217 swipe) remain available as a supplementary input path on
hardware but are no longer the primary mechanism.

---

## Build Environments

Two environments exist in `platformio.ini`. Never mix their libraries or code paths.

### `[env:wokwi]` — Simulation (current active env)
- Board: `esp32-s3-devkitc-1`
- Display: ILI9341 240×320 via SPI (Wokwi proxy — rectangular, NOT round)
- Buttons: Two GPIO pushbuttons — PIN_A=16, PIN_B=17
- Audio: **No-op** — Tones stub only
- Touch: Not used — button GPIOs replace gestures
- Build flag: `-DWOKWI_SIM=1`
- Libraries: Adafruit ILI9341, Adafruit GFX, Adafruit BusIO

### `[env:waveshare]` — Real hardware
- Board: `esp32-s3-devkitc-1` or equivalent ESP32-S3 target
- Display: CO5300 466×466 ROUND AMOLED via QSPI — rendered with **Arduino_GFX**
- Display rotation: `setRotation(1)` — 90° clockwise so buttons are at 12 o'clock
- Buttons: **Button A** = PWR via AXP2101 IRQ; **Button B** = GPIO0 (BOOT button)
- Audio: ES8311 via I2S — full tone generation
- Touch: CST9217 via SensorLib — swipe gestures as supplementary input only
- Build flag: `-DWAVESHARE_HW=1` (no `WOKWI_SIM` flag)
- Libraries: Arduino_GFX_Library, XPowersLib, SensorLib

> See `docs/HARDWARE_ENV.md` for the complete `[env:waveshare]` platformio.ini block
> and Arduino board manager URL.

---

## Library Rules — CRITICAL

### Hardware env (`WAVESHARE_HW`)

| Purpose          | USE                          | DO NOT USE                          |
|------------------|------------------------------|-------------------------------------|
| Display render   | `Arduino_GFX_Library`        | Adafruit GFX, M5GFX, TFT_eSPI      |
| Power / battery  | `XPowersLib` (AXP2101)       | Direct I2C register writes to 0x34  |
| Touch / sensors  | `SensorLib` (CST9217, QMI8658, PCF85063) | Wire reads direct to 0x5A  |
| Audio codec      | `ESP8311` via I2S (see `Tones.cpp`) | M5Unified speaker, tone()      |
| Preferences/NVS  | `Preferences` (Arduino)      | SPIFFS, LittleFS                    |

### Wokwi sim env (`WOKWI_SIM`)

| Purpose          | USE                          | DO NOT USE                          |
|------------------|------------------------------|-------------------------------------|
| Display render   | `Adafruit_ILI9341`           | Arduino_GFX, M5GFX                  |
| Buttons          | `digitalRead(PIN_A/B)`       | XPowersLib, SensorLib               |
| Audio            | Stub only — no output        | I2S, ES8311, any audio lib          |
| Touch            | Not applicable in sim        | CST9217, SensorLib touch            |

### Always forbidden (either env)

- `M5Unified`, `M5GFX`, `M5IOE1`, `M5PM1` — M5Stack ecosystem, wrong hardware
- `delay()` inside audio callbacks or display update loops — blocks the timer
- `Wire.begin()` called more than once — shared bus, initialise once in `setup()`
- Any library not listed above without first updating this file

---

## GPIO Pin Reference

> The canonical header is `include/pin_config.h`. Do not hardcode these values
> anywhere else in the codebase.

```
Display (CO5300 QSPI)         Touch (CST9217 I2C)
  LCD_SDIO0   GPIO4             IIC_SDA    GPIO15
  LCD_SDIO1   GPIO5             IIC_SCL    GPIO14
  LCD_SDIO2   GPIO6             TP_INT     GPIO11
  LCD_SDIO3   GPIO7             TP_RESET   GPIO40
  LCD_SCLK    GPIO38
  LCD_CS      GPIO12          Audio (ES8311 I2S)
  LCD_RESET   GPIO39            I2S_MCLK   GPIO42
                                I2S_BCLK   GPIO9
I2C Bus (shared — all chips)    I2S_WS     GPIO45
  SDA         GPIO15            I2S_DO     GPIO10
  SCL         GPIO14            I2S_DI     GPIO8
                                PA_EN      GPIO46 (active HIGH)
Buttons (hardware)            Buttons (Wokwi sim)
  BTN_A  AXP2101 power-key     BTN_A_SIM   GPIO16
  BTN_B  GPIO0  (BOOT button)  BTN_B_SIM   GPIO17
```

---

## Input Architecture

### Hardware (`WAVESHARE_HW`)

**Button A — PWR button**
- Physical position: top-left when device is rotated 90° CW (stopwatch orientation)
- Read via AXP2101 PKEY interrupt using `XPowersLib`
- Long press intentionally NOT intercepted — let AXP2101 handle power-off
- Implementation in `Buttons::update()`:
  - Enable all four PKEY IRQ types (SHORT, LONG, NEGATIVE, POSITIVE) so INTEN2 bits 0-3 are set
  - Call `disableSleep()` on init — sleep mode suppresses PKEY events
  - Read `getIrqStatus()` and check `irqStatus & 0x0F00` (INTSTS2 bits 0-3) to detect
    any PKEY event, but only register a click when `irqStatus & 0x0100` (POSITIVE/release
    edge, `_BV(8)`) is set — this way holding L to power off never triggers a click
    because the AXP2101 powers the device off before the release IRQ can fire
  - `clearIrqStatus()` is called on any PKEY event so NEGATIVE bits don't linger
  - **200 ms cooldown** after each click as a guard against bouncy POSITIVE edges

**Button B — BOOT button (GPIO0)**
- Physical position: top-right when device is rotated 90° CW (stopwatch orientation)
- GPIO0 with internal pull-up enabled — active LOW when pressed
- Short press = scratch / secondary action
- Long press (800ms) = abort round / end working time early
- Enable as input in `setup()` after the boot strapping window has passed:
  ```cpp
  pinMode(BTN_B_HW_PIN, INPUT_PULLUP);   // BTN_B_HW_PIN = 0
  ```
- Standard `digitalRead` + debounce in `Buttons::update()` — same pattern as sim
- **Startup caveat:** do not hold GPIO0 LOW while powering on. Document for users.

**Touch gestures (CST9217) — supplementary only**
- Swipe left  → same as Button B short press (scratch)
- Swipe down  → same as Button B long press (abort)
- Gesture reads via `SensorLib` `SensorCST9217`
- Touch coordinate system is rotated with the display — apply 90° CW transform:
  ```cpp
  // Raw CST9217 reports x,y for 0° orientation; after setRotation(1):
  int rotX = touch_y_raw;
  int rotY = (466 - touch_x_raw);
  ```

### Wokwi sim (`WOKWI_SIM`)

- GPIO16 (green button) = Button A
- GPIO17 (blue button)  = Button B
- Same `Buttons` API — `#ifdef WOKWI_SIM` guard selects the implementation

### Debounce constants (both envs)

```cpp
DEBOUNCE_MS      50
LONG_PRESS_MS   800
SWIPE_MIN_PX     40   // minimum swipe distance to register (hardware only)
SWIPE_MAX_MS    500   // maximum swipe duration to register
```

---

## Application State Machine

R = BOOT button (right, primary) — `btnB` in Buttons API, `btnR` in main.cpp
L = PWR button (left, secondary) — `btnA` in Buttons API, `btnL` in main.cpp
"R hold" = 800ms (LONG_PRESS_MS). "R hold 2s" = very long press (VERY_LONG_PRESS_MS).

```
IDLE
  → [R hold]        → SETTINGS
  → [R click]       → FLIGHT_RUNNING  (starts WT + flight timer simultaneously)
  → [L click]       → WORKING_TIME_RUNNING (starts WT only — wait for pilot to launch)
  → [base: COUNT N] → COUNTDOWN

PILOT_SELECT  (entered when base sends PILOTS command)
  → [R click]       → scroll to next pilot
  → [L click]       → scroll to previous pilot
  → [R hold]        → IDLE (pilot confirmed)
  → [base: COUNT N] → COUNTDOWN

COUNTDOWN  (base sends COUNT 10..1 during last 10s of prep)
  → [base: COUNT N] → update arc display (no button input)
  → [base: START]   → WORKING_TIME_RUNNING (+ long beep)
  Displays: green anticlockwise arc, large countdown number, short beep per tick

WORKING_TIME_RUNNING
  → [R click]       → FLIGHT_RUNNING  (start next flight)
  → [L click]       → SCRATCH_CONFIRM (if flights exist — scratch last recorded flight)
  → [R hold 2s]     → WORKING_TIME_EXPIRED (abort round)
  → [WT expired]    → WORKING_TIME_EXPIRED

FLIGHT_RUNNING
  → [R click]       → WORKING_TIME_RUNNING (stop flight, record time)
  → [R hold 2s]     → WORKING_TIME_EXPIRED (abort round, discard in-progress flight)
  → [WT expired]    → WORKING_TIME_EXPIRED (auto-stop and record flight)

SCRATCH_CONFIRM
  → [R click]       → WORKING_TIME_RUNNING (confirmed — flight scratched)
  → [timeout 2s]    → WORKING_TIME_RUNNING (cancelled — no change)
  → [R hold 2s]     → WORKING_TIME_EXPIRED (abort round)
  → [WT expired]    → WORKING_TIME_EXPIRED

WORKING_TIME_EXPIRED
  → [R click]       → ALTITUDE_ENTRY (F5K mode, if flights recorded)
  → [R click]       → IDLE (F3K mode or no flights)

ALTITUDE_ENTRY  (F5K only — enter launch altitude per flight after round)
  → [R click]       → ones digit +1m (rolls 0→9→0; no-op at 100m)
  → [L click]       → tens digit +10m (rolls 0→10→…→100→0; ones preserved)
  → [R hold]        → confirm altitude, advance to next flight (or IDLE when all done)
  Max altitude 100m. Ones and tens roll independently — no carry between digits.

SETTINGS  (page 1 of 3: working time)
  → [R click]       → +1 minute
  → [L click]       → -1 minute
  → [R hold]        → TASK_SELECT (advance to page 2)
  → [8s inactivity] → TASK_SELECT (auto-advance)

TASK_SELECT  (page 2 of 3: task type)
  → [R or L click]  → toggle F3K / F5K
  → [R hold]        → OTA_CHECK (advance to page 3)
  → [3s inactivity] → OTA_CHECK (auto-advance)

OTA_CHECK  (page 3 of 3: firmware update — hardware only)
  → [on entry]      → async version check via HTTP to base station :8080
  → [R hold]        → start OTA download + flash (only when update is available)
  → [R click]       → IDLE (exit)
  → [8s inactivity] → IDLE (auto-exit)
  States: CHECKING... | UP TO DATE | fw-vN AVAIL | LOADING X% | DONE REBOOTING | FAILED | NO WIFI
  OTA is disabled in Wokwi sim (TASK_SELECT goes directly to IDLE in sim build)
```

---

## Display

### Overview of current implementation state

- **`[env:wokwi]` path in `UI.cpp`**: Fully implemented for ILI9341 240×320 rectangular display.
- **`[env:waveshare]` path in `UI.cpp`**: **Fully implemented and working on real hardware.** Uses `Arduino_Canvas` for software rotation (CO5300 does not support hardware rotation). Custom `ws_fillRing()` and `ws_eraseRingRadial()` helpers work around CO5300 QSPI not supporting `writeFastHLine`. Typography uses FreeFonts from the GFX Library for Arduino (resolved via the library include path — the `fonts/` directory in the lib provides them).

### Config constants (set per environment via build flags)

```cpp
// Hardware (waveshare)
DISPLAY_WIDTH    466     DISPLAY_CX   233
DISPLAY_HEIGHT   466     DISPLAY_CY   233
ARC_OUTER_RADIUS 220     ARC_INNER_RADIUS 200

// Wokwi sim
DISPLAY_WIDTH    240     DISPLAY_CX   120
DISPLAY_HEIGHT   320     DISPLAY_CY   160
ARC_OUTER_RADIUS 110     ARC_INNER_RADIUS  95
```

### Display rotation (waveshare only)

`setRotation(1)` is called once in `UI::begin()` under the `WAVESHARE_HW` guard.
This rotates content 90° clockwise so the physical buttons sit at 12 o'clock.

For the round display, rotation does not change the circle geometry — the safe zone
radii and centre point (233, 233) remain valid. It only changes which physical edge
is visually "top". Status elements (battery, gesture hints) should be positioned
relative to the rotated top.

### Arc behaviour (both envs)

- Full circle = 360° = start of working time
- Arc sweeps CCW as time counts down
- Colour thresholds (seconds remaining): Green >60s, Orange 30–60s, Red <30s
- Flashing at 250ms intervals when <10s remaining
- Arc drawn with `fillArc()` (Arduino_GFX) or equivalent

---

## Round Display Constraints — WAVESHARE HARDWARE ONLY

> **These rules apply exclusively to the `[env:waveshare]` render path.**
> The Wokwi ILI9341 proxy is rectangular — these constraints do NOT apply there.
> When building any new screen for waveshare, read this section first. Every draw
> call must comply before commit.

### Physical facts

- Resolution: 466 × 466 px (square framebuffer, round physical clip)
- Centre: (233, 233) — all layout is radially symmetric about this point
- Physical clip radius: 233 px — hardware clips the framebuffer to a circle
- CO5300 driver writes to the full 466×466 buffer. The corners ARE written but are
  physically absent on the real panel. They look fine in Wokwi sim (rectangular proxy).
- **Wokwi does NOT enforce the round clip** — corner content appears valid in sim but
  is invisible on real hardware. Always validate new screens on the device.

### Safe zones

```
r = 0–90   px  →  CENTRE ZONE      Primary readout only (main clock, flight time)
r = 90–160 px  →  INNER RING       Secondary info (state label, window type)
r = 160–210 px →  OUTER RING       Status indicators (battery, round/group, gesture cues)
r = 210–233 px →  EDGE MARGIN      Reserved — never place text or interactive elements here
r > 233 px     →  DEAD ZONE        Off-screen on hardware — never render here
```

### Hard rules — never violate

1. **No rectangular full-screen fills** — use `gfx->fillCircle(233, 233, 233, colour)`
   for background clears, not `fillRect(0, 0, 466, 466, ...)`
2. **All text must be centre-anchored at x=233** unless explicitly arc-curved along a ring
3. **No element (text, icon, arc, rect) may have any pixel outside r=210** (safe boundary)
4. **No rectangular UI chrome** — no borders, boxes, or frames with right-angle corners
   unless they fit entirely within r=210 (e.g. a small centred dialog box is fine)
5. **Clip circle is a software design responsibility** — always verify new draw calls
   against the r=210 safe boundary before shipping
6. **No rectangular progress bars** — use arc segments instead
7. **No left-aligned text anchored to x=0 or x=10** — will fall outside safe zone

### Debug arcs (use during development, remove before release)

```cpp
// Draw during development to visualise boundaries — remove before release
gfx->drawCircle(233, 233, 233, 0x07E0); // hard clip boundary (green)
gfx->drawCircle(233, 233, 210, 0x02D0); // safe content boundary (teal)
gfx->drawCircle(233, 233, 160, 0x02D0); // inner ring boundary
gfx->drawCircle(233, 233, 90,  0x02D0); // centre zone boundary
```

### Typography scale

| Zone          | Use                      | Min size | Notes                                  |
|---------------|--------------------------|----------|----------------------------------------|
| Centre zone   | Primary time (WT or FT)  | 80 px    | Must be readable at arm's length, sun  |
| Inner ring    | State label, window type | 22–28 px | e.g. "WORK", "PREP", "LAND"            |
| Outer ring    | Status, battery, hints   | 16–20 px | Small but not tiny                     |

- All numeric readouts must be **monospaced** (fixed-width font) to prevent layout shift
  as digits change.
- Minimum legible size at arm's length in sunlight: **22 px**

### Colour system (flight window states)

| Meaning              | Colour       | Usage                                     |
|----------------------|--------------|-------------------------------------------|
| AMOLED background    | `0x000000`   | Always — true black = zero power on dark  |
| Primary readout      | `0xFFFFFF`   | Main clock digits (white)                 |
| WORK window active   | `0x00FF88`   | Outer arc / state label (green)           |
| PREP / TEST window   | `0x00AAFF`   | Outer arc / state label (blue)            |
| LAND window          | `0xFFAA00`   | Outer arc / state label (amber)           |
| Launch FORBIDDEN     | `0xFF3333`   | Background arc — high urgency (red)       |
| Scratch / invalid    | `0xFF3333`   | Strike-through or icon                    |
| Battery warning      | `0xFF6600`   | Battery indicator only                    |
| Gesture hint overlay | `0x444444`   | Subtle — fades after 2 s                  |
| Arc green (>60s WT)  | `0x00C800`   | Working time arc, healthy                 |
| Arc orange (30–60s)  | `0xFF8800`   | Working time arc, caution                 |
| Arc red (<30s)       | `0xFF2020`   | Working time arc, urgent                  |

### Radial layout template (waveshare screens)

After `setRotation(1)`, the physical buttons are at the top. "Top of display" means
the 12 o'clock position (y < 233 in rotated frame).

```
Outer ring (r 185–205):
  ├── 12 o'clock (top):    battery % — closest to physical buttons
  ├── 10 o'clock:          round/group identifier (e.g. "R1A")
  └── 6 o'clock (bottom):  gesture hint (swipe cues — fade after 2 s)

Inner ring (r 110–160):
  ├── 12 o'clock:          state label ("WORKING TIME" / "FLIGHT ACTIVE" etc.)
  └── 6 o'clock:           task name or flight count

Centre zone (r 0–80):
  └── Primary:             large countdown or flight time (80–100 px font)
      Sub-line:            secondary timer if relevant (40–50 px font)

Arc ring (r ARC_INNER_RADIUS–ARC_OUTER_RADIUS):
  └── CCW sweep:           working time progress (colour by threshold)
```

### Waveshare render path — implementation guidance

When implementing `UI.cpp` for `WAVESHARE_HW`:

- Write all waveshare render functions inside `#else` of `#ifdef WOKWI_SIM` guards
- Do NOT share Y-coordinate constants between wokwi and waveshare paths
- Define waveshare layout constants separately:
  ```cpp
  #ifdef WAVESHARE_HW
  static const int WS_CX = 233;
  static const int WS_CY = 233;
  static const int WS_R_CENTRE    = 80;   // centre zone outer edge
  static const int WS_R_INNER     = 160;  // inner ring outer edge
  static const int WS_R_OUTER     = 210;  // outer ring / safe boundary
  static const int WS_Y_PRIMARY   = 233;  // vertically centred primary time
  static const int WS_Y_STATE     = 150;  // state label (inner ring, rotated top)
  static const int WS_Y_SUBLABEL  = 316;  // inner ring bottom (rotated)
  static const int WS_Y_BATTERY   = 60;   // outer ring top (near buttons)
  #endif
  ```
- Background clear: `gfx->fillCircle(233, 233, 233, COL_BG)` — NOT fillScreen or fillRect
- Use `gfx->drawArc()` or custom `ArcRenderer` for the working time ring
- The `ArcRenderer` class already exists — extend it for the waveshare arc API
- Call `gfx->setRotation(1)` once in `UI::begin()` under `#ifdef WAVESHARE_HW`

---

## Audio Alert Schedule

No vibration motor — audio only.

| Time Remaining | Pattern              | Freq (Hz) | Duration   |
|----------------|----------------------|-----------|------------|
| 30s            | Single beep          | 880       | 150ms      |
| 15s            | Double beep          | 880       | 100ms × 2  |
| 10s            | Single beep          | 1046      | 100ms      |
| 9s – 6s        | Single beep (each)   | 1046      | 100ms      |
| 5s – 3s        | Single beep (each)   | 1175      | 100ms      |
| 2s             | Single beep          | 1319      | 100ms      |
| 1s             | Long low tone        | 440       | 400ms      |
| 0s             | Descending tone      | 880→440   | 800ms      |

Additional tones:

| Event                          | Function            | Freq (Hz) | Duration |
|-------------------------------|---------------------|-----------|----------|
| Countdown tick (COUNT N)       | `playAlert(N)`      | N × 110   | 100ms    |
| Window open (START from COUNT) | `playWindowOpen()`  | 1200      | 1200ms   |

- Alert triggers are checked in `WorkingTime::update()` — fire-once flags per point
- Tone generation: I2S DMA with sine wave buffer (hardware); stub (Wokwi)
- Speaker amp: `digitalWrite(PA_EN, HIGH)` before playback, `LOW` when idle
- **Never use `delay()` in tone generation** — use non-blocking DMA or FreeRTOS task

---

## Code Guard Pattern

All hardware-specific code must be wrapped:

```cpp
#ifdef WOKWI_SIM
    // Wokwi / ILI9341 / GPIO button path — rectangular display
#else
    // Waveshare hardware / CO5300 / AXP2101+GPIO0 / touch gesture path — ROUND display
#endif
```

The `Buttons` class is the primary place this pattern appears. `UI.cpp` uses it
for display initialisation (different GFX constructors and render logic). `Tones.cpp`
uses it for I2S vs stub. Keep the guard at the implementation level — the `.h`
interfaces stay identical between environments.

---

## Base Station TCP Protocol

Timer connects to base station AP (F3K_BASE) and opens a TCP connection to port 8765.
All messages are newline-terminated ASCII. `TimerComms.h/.cpp` handles this.

### Timer → Base

| Message                        | When                                |
|-------------------------------|-------------------------------------|
| `JOIN mac=<MAC>`               | On connect / reconnect              |
| `FLIGHT pilot=<id> dur=<ms>`  | After pilot stops a flight          |
| `PING`                         | Every 30s keepalive                 |

### Base → Timer

| Message                        | Timer action                                         |
|-------------------------------|------------------------------------------------------|
| `ASSIGN id=<N>`                | Store timer ID; triggers `send_catchup()` on reconnect |
| `PILOTS <id>:<name>,...`       | Build pilot list → enter `STATE_PILOT_SELECT`        |
| `TASK wt=<seconds>`            | Set working time before START                        |
| `START`                        | Start round; if in `STATE_COUNTDOWN`, play long beep |
| `STOP`                         | Stop round, go to `STATE_WORKING_TIME_EXPIRED`       |
| `COUNT <N>`                    | 10..1 countdown during last 10s of prep → `STATE_COUNTDOWN` |
| `PONG`                         | Response to PING (no action required)                |

### Reconnect behaviour (`send_catchup`)

When a timer reconnects mid-round, the base immediately calls `send_catchup()`:
- Always sends `PILOTS` if in any non-IDLE state
- Also sends `TASK wt=...` + `START` if state is WORKING

This means a timer that drops WiFi during PREP will rejoin in pilot-select; one that
drops during WORKING will rejoin and start the full working time from scratch
(known limitation — no timestamp recovery yet).

---

## Project Structure

```
f3k-timer/
  CLAUDE.md                   <- this file -- read before any change
  platformio.ini              <- [env:wokwi] active; [env:waveshare] in docs/HARDWARE_ENV.md
  partitions_f3k_ota.csv      <- custom 16 MB dual-OTA partition table (ota_0+ota_1, 3 MB each)
  wokwi.toml                  <- Wokwi simulator config
  diagram.json                <- Wokwi circuit diagram
  firmware/
    releases/                 <- last 5 compiled waveshare builds, each in fw-vN/ subfolder
                                 contents: firmware.bin, bootloader.bin, partitions.bin, release.txt
    ota/                      <- current OTA-served files (base station pulls from git at home)
                                 firmware.bin  -- same binary as latest release
                                 version.json  -- {"version":"fw-vN","url":"http://192.168.10.1:8080/ota/firmware.bin"}
  include/
    config.h                  <- task types, timing constants, AppState + OtaStatus enums
    fw_version.h              <- auto-generated by /release-firmware skill; defines FW_VERSION "fw-vN"
    pin_config.h              <- ALL GPIO defines -- do not hardcode pins elsewhere
  src/
    main.cpp                  <- setup(), loop(), state machine
    timer/
      WorkingTime.h/.cpp      <- countdown, alert trigger logic
      FlightTimer.h/.cpp      <- individual flight stopwatch
      FlightLog.h/.cpp        <- flight list, scratch, best-time index
    display/
      UI.h/.cpp               <- screen layout, render dispatcher
                                 Wokwi path: implemented (rectangular ILI9341)
                                 Waveshare path: fully implemented (round CO5300, radial layout)
      ArcRenderer.h/.cpp      <- CCW arc ring, colour thresholds
    input/
      Buttons.h/.cpp          <- PWR+GPIO0 (HW) or GPIO16/17 (sim) -- same API
    audio/
      Tones.h/.cpp            <- I2S sine wave alerts (HW) or stub (sim)
    ota/
      OtaUpdater.h/.cpp       <- async HTTPUpdate + version check via FreeRTOS tasks (HW only)
    storage/
      FlashStorage.h/.cpp     <- NVS persistence via Preferences
    comms/
      TimerComms.h/.cpp       <- WiFi TCP client: JOIN, PILOTS, COUNT, TASK, START, STOP, FLIGHT, PING
  docs/
    HARDWARE_ENV.md           <- [env:waveshare] platformio block + flash instructions
    INPUT_DESIGN.md           <- touch gesture spec, button ergonomics rationale
    ESP32_OTA_Reference.md    <- field-tested OTA do's/don'ts for ESP32 projects
```

---

## Known Issues / Watch-outs

1. **PWR via AXP2101 — do NOT use `isPekey*Irq()` helpers or `setPowerKeyPressOnTime()`**
   - The `isPekey*Irq()` helpers gate on a software `intRegister[]` cache that can
     desync from hardware; use `getIrqStatus() & 0x0F00` directly instead.
   - `setPowerKeyPressOnTime()` writes to AXP2101 register 0x27 which lives in
     **battery-backed RAM** (survives reflash). Writing 0x00 to this register corrupts
     the SHORT_IRQ state machine. Corrupted registers also silently break battery % ADC.
     **Never call `setPowerKeyPressOnTime()`** — leave register 0x27 at OTP defaults.
   - If L button stops working and battery % reads 0: physical battery disconnection
     (30+ seconds, draining AXP2101 capacitors) forces OTP reload and restores all registers.
   - `_pmu.reset()` (`setRegisterBit(0x10, 1)`) does NOT cause an SoC power cycle on
     this hardware — it has no observable effect.
   - AXP2101 fires BOTH NEGATIVE (press) and POSITIVE (release) PKEY IRQs per button
     cycle. Without a cooldown, each tap registers twice. Use a 200ms `_lastAClickMs`
     guard in `Buttons::update()`.
   - Long-press power-off is handled by AXP2101 hardware — do not intercept it in firmware.

2. **BOOT (GPIO0) as Button B** — GPIO0 is a strapping pin. Held LOW at power-on =
   download mode. Enable with `pinMode(0, INPUT_PULLUP)` in `setup()` only. A brief
   press during normal operation is safe. Startup caveat: do not hold Button B while
   powering on. The 90° CW display rotation places this button at top-right, making it
   ergonomically natural as a stopwatch secondary button.

3. **Single I2C bus** — all peripherals share GPIO14/15. Call `Wire.begin(14, 15)` once
   in `setup()`. Do not reinitialise per-peripheral.

4. **Round display clipping** — corners are physically absent on the real panel.
   Wokwi ILI9341 is rectangular — round clip is NOT enforced in sim.
   Always draw within r=210 from centre (233,233). Test on real hardware before finalising.

5. **PSRAM allocation** — 8MB available. Use `ps_malloc()` for audio DMA buffers and
   any large sprite buffers. Standard `malloc()` / stack allocation for small objects.

6. **Wokwi ILI9341 vs CO5300** — the SPI constructor signatures differ. The `UI.cpp`
   init block must be guarded with `#ifdef WOKWI_SIM`. Never share the constructor.

7. **Speaker amp enable** — `PA_EN` (GPIO46) must be driven HIGH before any I2S audio
   and LOW when idle. Forgetting this = no sound, no error.

8. **No vibration motor** — remove any haptic/vibration code that appears. Audio-only alerts.

9. **`delay()` in audio/display** — forbidden. Use `millis()` deltas, FreeRTOS tasks,
   or I2S DMA callbacks for any time-based audio work.

10. **Wokwi UI layout ≠ Waveshare UI layout** — The two render paths are fundamentally
    different. Wokwi uses Y-coordinate constants for a 240×320 portrait layout. Waveshare
    uses radial zone constants (WS_Y_*) for 466×466 round. Never share layout constants
    between the two paths.

11. **Touch coordinate rotation** — CST9217 reports raw coordinates for 0° orientation.
    After `setRotation(1)`, apply: `rotX = y_raw; rotY = (466 - x_raw)` before using
    touch coordinates for gesture detection or tap zone matching.

12. **Partition table change requires full flash erase** — fw-v11 switched from
    `default_16MB.csv` to `partitions_f3k_ota.csv` (OTA dual-slot layout). Any device
    running fw-v10 or earlier must be erased before flashing fw-v11+:
    ```powershell
    & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare -t erase --upload-port COM4 --project-dir "C:\Kris\Projects\F3K_Timer_1"
    ```
    After that, flash normally. Devices already on fw-v11+ can OTA-update without erasing.

13. **OTA version mismatch loop** — If `firmware/ota/firmware.bin` was built with a
    different `FW_VERSION` than `firmware/ota/version.json` claims, the device will
    always report an update available and re-flash itself with the same binary.
    Root cause: `fw_version.h` must be written with the new version BEFORE the build runs.
    The `/release-firmware` skill enforces this order. If you ever build manually, write
    `fw_version.h` first.

---

## Firmware Release Workflow

Versioned firmware snapshots live in `firmware/releases/` and are tagged in git as `fw-vN`.
The 5 most recent compiled builds are kept on disk; git tags for all versions are kept indefinitely.

### Creating a release

Use the `/release-firmware` Claude skill (invoke with `/release-firmware` at the end of a
session). It will:

1. Scan existing git tags to determine the next version (fw-vN)
2. Write `include/fw_version.h` with `#define FW_VERSION "fw-vN"` **before** building
3. Build the waveshare env
4. Copy `firmware.bin`, `bootloader.bin`, `partitions.bin` to `firmware/releases/fw-vN/`
5. Write `release.txt` with version, date, source commit hash, and esptool flash addresses
6. Trim to 5 most recent release folders
7. Copy `firmware.bin` and write `version.json` to `firmware/ota/` (base station OTA source)
8. Commit `firmware/` + `include/fw_version.h` then tag `fw-vN`

> **CRITICAL:** `fw_version.h` must be written BEFORE the build. The embedded `FW_VERSION`
> must match the git tag or the OTA check will loop — the device will always report an
> update available and re-flash with the same binary forever.

After the skill completes:
```powershell
git push; git push origin fw-vN
```

### Rolling back firmware

| Situation | Action |
|---|---|
| Need to revert the device quickly | `cd firmware\releases\fw-vN` then run esptool with addresses in `release.txt` |
| Need to inspect or rebuild old source | `git checkout fw-vN` |
| Compare source between versions | `git diff fw-v8 fw-v9` |

Compiled binaries exist from fw-v10 onward. fw-v1 through fw-v9 are source-only tags
(retrospectively applied) -- rebuilding requires `git checkout fw-vN` then a manual build.

---

## Build & Flash (hardware)

See `docs/HARDWARE_ENV.md` for full setup. Quick reference:

```powershell
# Wokwi sim (then use Wokwi VS Code extension to run)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e wokwi

# Hardware -- build only
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare

# Hardware -- build and flash (device must be in download mode: hold BOOT, tap RESET, release BOOT)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e waveshare --target upload --upload-port COM4 --project-dir "C:\Kris\Projects\F3K_Timer_1"

# Serial monitor
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --environment waveshare --baud 115200 --project-dir "C:\Kris\Projects\F3K_Timer_1"
```

For a release flash (end of session), prefer the release script over the raw pio command --
it snapshots the binary, commits, and tags in one step.
