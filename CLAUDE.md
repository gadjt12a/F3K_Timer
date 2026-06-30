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

| Physical button | GPIO / source | Role            |
|-----------------|---------------|-----------------|
| PWR (top-left when rotated) | AXP2101 power-key IRQ | Button A — primary (start/stop flight) |
| BOOT (top-right when rotated) | GPIO0 | Button B — secondary (scratch / abort) |

> **GPIO0 strapping pin note:** GPIO0 held LOW at power-on forces download mode.
> Enable it as an input only after `setup()` has run — the bootloader window closes
> before user code starts. A brief press during normal operation is completely safe.
> Document for users: do not hold Button B while pressing Reset/Power-on.

Touch gestures (CST9217 swipe) remain available as a supplementary input path on
hardware but are no longer the primary Button B mechanism.

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
- Read via AXP2101 power-key interrupt using `XPowersLib`
- Short press = primary action (start/stop)
- Long press intentionally NOT intercepted — let AXP2101 handle power-off
- Poll `pmu.isPekeyShortPressIRQ()` in `Buttons::update()`

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

```
IDLE
  → [A click]       → FLIGHT_RUNNING  (starts WT + flight timer simultaneously)
  → [B hold]        → SETTINGS

WORKING_TIME_RUNNING
  → [A click]       → FLIGHT_RUNNING  (start next flight)
  → [B click]       → SCRATCH_CONFIRM (if flights exist)
  → [B hold]        → WORKING_TIME_EXPIRED (abort round)
  → [WT expired]    → WORKING_TIME_EXPIRED

FLIGHT_RUNNING
  → [A click]       → WORKING_TIME_RUNNING (stop flight, record time)
  → [B hold]        → WORKING_TIME_EXPIRED (abort round)
  → [WT expired]    → WORKING_TIME_EXPIRED (auto-stop flight)

SCRATCH_CONFIRM
  → [B click]       → WORKING_TIME_RUNNING (flight scratched)
  → [timeout 2s]    → WORKING_TIME_RUNNING (cancel)
  → [B hold]        → WORKING_TIME_EXPIRED (abort round)
  → [WT expired]    → WORKING_TIME_EXPIRED

WORKING_TIME_EXPIRED
  → [A click]       → IDLE

SETTINGS
  → [A click]       → +1 minute
  → [B click]       → -1 minute
  → [A hold]        → IDLE (confirm)
  → [8s inactivity] → IDLE (auto-confirm)
```

---

## Display

### Overview of current implementation state

- **`[env:wokwi]` path in `UI.cpp`**: Fully implemented for ILI9341 240×320 rectangular display.
  Y coordinates are hard-coded for portrait rectangular layout (Y_WT_LABEL=80, etc.).
- **`[env:waveshare]` path in `UI.cpp`**: **NOT YET IMPLEMENTED.**
  When building the waveshare render path, write it from scratch using radial/zone layout
  described in the Round Display Constraints section below. Do NOT adapt the Wokwi Y-coordinate
  layout — it is fundamentally rectangular and will look wrong on a round display.

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

## Project Structure

```
f3k-timer/
  CLAUDE.md               ← this file — read before any change
  platformio.ini          ← [env:wokwi] active; [env:waveshare] in docs/HARDWARE_ENV.md
  wokwi.toml              ← Wokwi simulator config
  diagram.json            ← Wokwi circuit diagram
  include/
    config.h              ← task types, timing constants (env-aware via build flags)
    pin_config.h          ← ALL GPIO defines — do not hardcode pins elsewhere
  src/
    main.cpp              ← setup(), loop(), state machine
    timer/
      WorkingTime.h/.cpp  ← countdown, alert trigger logic
      FlightTimer.h/.cpp  ← individual flight stopwatch
      FlightLog.h/.cpp    ← flight list, scratch, best-time index
    display/
      UI.h/.cpp           ← screen layout, render dispatcher
                            Wokwi path: implemented (rectangular ILI9341)
                            Waveshare path: TO BE BUILT (round CO5300, radial layout)
      ArcRenderer.h/.cpp  ← CCW arc ring, colour thresholds
    input/
      Buttons.h/.cpp      ← PWR+GPIO0 (HW) or GPIO16/17 (sim) — same API
    audio/
      Tones.h/.cpp        ← I2S sine wave alerts (HW) or stub (sim)
    storage/              ← Phase 2
      FlashStorage.h/.cpp ← NVS persistence via Preferences
    comms/                ← Phase 3: ESP-NOW
      ESPNow.h/.cpp
  docs/
    HARDWARE_ENV.md       ← [env:waveshare] platformio block + flash instructions
    INPUT_DESIGN.md       ← touch gesture spec, button ergonomics rationale
```

---

## Known Issues / Watch-outs

1. **PWR via AXP2101** — read power-key via `pmu.isPekeyShortPressIRQ()` after calling
   `pmu.clearIRQ()`. Do not `digitalRead` the PWR pin. Long-press power-off is handled
   by AXP2101 hardware — do not intercept it in firmware.

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

10. **Wokwi UI layout ≠ Waveshare UI layout** — `UI.cpp` currently only has the Wokwi
    (rectangular) render path. When implementing the waveshare path, do NOT adapt the
    Wokwi Y-coordinate layout. Build it from scratch using the radial zone system
    defined in "Round Display Constraints" above.

11. **Touch coordinate rotation** — CST9217 reports raw coordinates for 0° orientation.
    After `setRotation(1)`, apply: `rotX = y_raw; rotY = (466 - x_raw)` before using
    touch coordinates for gesture detection or tap zone matching.

---

## Build & Flash (hardware)

See `docs/HARDWARE_ENV.md` for full setup. Quick reference:

```bash
# Wokwi sim
pio run -e wokwi
# (then use Wokwi VS Code extension to run)

# Hardware build (once env:waveshare is configured)
pio run -e waveshare
pio run -e waveshare --target upload
pio device monitor -e waveshare --baud 115200
```

Flash mode: hold BOOT, tap RESET, release BOOT — device enumerates as USB serial.
