# Input Design
## Button & Touch Gesture Specification

This document records the ergonomic rationale and full technical specification
for all input on the Waveshare ESP32-S3-Touch-AMOLED-1.75C.

---

## Hardware Context

The device is worn on the wrist or held in the palm by a timekeeper standing
on the field watching aircraft. One hand holds the device; the other may be
free. Eyes are on the sky, not the screen. Input must be:
- Operable by feel / muscle memory
- Low false-positive rate (no accidental state changes)
- Distinguishable gestures that don't conflict mid-flight

### Physical button layout

```
        ┌──────────────────┐
        │                  │
        │   (round screen) │
        │                  │
        └──────────────────┘
                 │
        [PWR]  ← top-right edge   ← BUTTON A
        [BOOT] ← bottom-right edge ← dev only, not used in-flight
```

---

## Button A — PWR (top right)

**Physical:** momentary button, top-right edge of the aluminium enclosure.
Thumb-reachable when held in right hand, index-reachable in left hand.

**Read method:** AXP2101 power-key IRQ via XPowersLib.
```cpp
pmu.isPekeyShortPressIRQ()   // short press
```

**Actions:**
- Short press → primary action in current state (start, stop flight, confirm)
- Long press → **DO NOT intercept** — AXP2101 handles power-off in hardware

**Why not intercept long-press for an app function?**
The AXP2101 PMIC fires a hardware power-off sequence on a sustained hold (~3s).
Attempting to use long-press for an app function risks fighting the PMIC or causing
an unexpected shutdown in competition.

---

## Button B — Touch Gestures (replacing BOOT)

**Why BOOT is not usable in-flight:**
- Located at the bottom right of the enclosure — falls in the palm when held
- GPIO0 is a strapping pin — any hold at power-on enters the bootloader
- Cannot be used for any gesture requiring the button held at startup

**Replacement:** Swipe gestures on the CST9217 touchscreen.

### Gesture specification

| Gesture      | Threshold                          | Maps to      | App action                        |
|--------------|------------------------------------|--------------|-----------------------------------|
| Swipe left   | dx ≤ −40px, dy < 30px, ≤ 500ms    | btnB click   | Scratch last flight / settings −1 |
| Swipe down   | dy ≥ +40px, dx < 30px, ≤ 500ms    | btnB hold    | Abort round / end working time    |
| Tap (single) | contact < 150ms, movement < 10px  | (reserved)   | Future: menu confirm              |

**Constants (defined in `config.h`):**
```cpp
#define SWIPE_MIN_PX    40     // minimum travel to register as a swipe
#define SWIPE_MAX_MS   500     // maximum duration for a swipe gesture
#define SWIPE_AXIS_PX   30     // maximum off-axis movement before gesture is rejected
#define TAP_MAX_MS     150     // maximum contact duration for a tap
#define TAP_MAX_MOVE    10     // maximum movement for a tap
```

### False-positive mitigation

- Swipe down (abort) requires dy ≥ 40px AND duration ≤ 500ms AND off-axis < 30px
- This rules out accidental brushes and slow drags
- Abort is a hold-equivalent — a momentary touch won't trigger it
- In `STATE_FLIGHT_RUNNING`, consider a guard: swipe down requires confirmation
  (same as SCRATCH_CONFIRM) if the flight is > 10s old — prevents accidental
  mid-flight abort. This is a future enhancement; not implemented in v1.

### Implementation in `Buttons::update()` (hardware path)

```cpp
#ifndef WOKWI_SIM
void Buttons::_pollGestures() {
    if (!_touch.isAvailable()) return;

    uint16_t x, y;
    _touch.getPoint(x, y);   // first touch point

    if (!_touchActive) {
        // touch start
        _touchStartX  = x;
        _touchStartY  = y;
        _touchStartMs = millis();
        _touchActive  = true;
        return;
    }

    // touch is held or just released
    if (_touch.isAvailable()) {
        // still touching — update current position
        _touchCurX = x;
        _touchCurY = y;
    } else {
        // released — evaluate gesture
        int dx = (int)_touchCurX - (int)_touchStartX;
        int dy = (int)_touchCurY - (int)_touchStartY;
        unsigned long dt = millis() - _touchStartMs;

        if (dt <= SWIPE_MAX_MS) {
            if (dx <= -SWIPE_MIN_PX && abs(dy) < SWIPE_AXIS_PX) {
                _clickB = true;   // swipe left → B click
            } else if (dy >= SWIPE_MIN_PX && abs(dx) < SWIPE_AXIS_PX) {
                _holdB = true;    // swipe down → B hold
            }
        }
        _touchActive = false;
    }
}
#endif
```

> **Note:** The `SensorCST9217` API may differ slightly — verify `isAvailable()`,
> `getPoint()`, and release detection against the SensorsLib header before
> implementing. Update this doc if the API differs.

---

## Wokwi Simulation Input

The sim uses two GPIO pushbuttons as a direct proxy:

| GPIO | Colour | Maps to    | Behaviour                          |
|------|--------|------------|------------------------------------|
| 16   | Green  | Button A   | Click on falling edge              |
| 17   | Blue   | Button B   | Click on release; hold at 800ms    |

Wokwi pushbuttons are toggle-style in the simulator — they stay pressed until
clicked again. The `Buttons` implementation fires click on the falling edge in
sim mode to work around this (see `Buttons.cpp`).

---

## `Buttons` Class API (unchanged across environments)

```cpp
bool btnAClicked();   // true for one update() cycle on short press
bool btnBClicked();   // true for one update() cycle on B click / swipe left
bool btnAHeld();      // true for one update() cycle on long press (≥800ms)
bool btnBHeld();      // true for one update() cycle on B hold / swipe down
```

The state machine in `main.cpp` uses only this API. It has no knowledge of whether
events come from GPIO, AXP2101, or touch gestures.

---

## Settings Screen Touch (future)

When the settings screen is redesigned for the round 466×466 display, the lower
quadrant of the screen (below centre) can host tap targets for confirm/cancel.
These are not part of the `Buttons` API — they will be handled directly in `UI.cpp`
by reading touch coordinates during `STATE_SETTINGS` rendering.

Not implemented in v1 — use Button A / swipe left for settings navigation.

---

## Change Log

| Date       | Change                                               |
|------------|------------------------------------------------------|
| 2025-06    | Initial spec — hardware switched from M5Stack to Waveshare |
| 2025-06    | BOOT button ruled out; swipe gestures adopted        |
