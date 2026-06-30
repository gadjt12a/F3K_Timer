# Hardware Environment Setup
## `[env:waveshare]` — Waveshare ESP32-S3-Touch-AMOLED-1.75C

This document covers everything needed to configure and build for real hardware.
The Wokwi sim env (`[env:wokwi]`) is already configured in `platformio.ini` and
should not be modified.

---

## Arduino Board Manager URL

Add to PlatformIO / Arduino IDE board manager (or set in `platformio.ini`):

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Required: **arduino-esp32 core ≥ 3.0.0** (ESP32-S3 support with QSPI display and USB CDC).

---

## `platformio.ini` Block to Add

Append this to the existing `platformio.ini` (below `[env:wokwi]`):

```ini
; ── Waveshare ESP32-S3-Touch-AMOLED-1.75C — real hardware ─────────────────
[env:waveshare]
platform          = espressif32 @ 6.12.0
board             = esp32-s3-devkitc-1
framework         = arduino
monitor_speed     = 115200
board_build.flash_size        = 16MB
board_build.partitions        = default_16MB.csv
board_build.arduino.memory_type = qio_opi   ; 8MB OPI PSRAM
board_upload.flash_size       = 16MB

build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DWAVESHARE_HW=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    ; Display geometry — 466×466 round AMOLED
    -DDISPLAY_WIDTH=466
    -DDISPLAY_HEIGHT=466
    -DDISPLAY_CX=233
    -DDISPLAY_CY=233
    -DARC_OUTER_RADIUS=220
    -DARC_INNER_RADIUS=200

lib_deps =
    ; Display — CO5300 QSPI via Arduino_GFX
    https://github.com/moononournation/Arduino_GFX.git
    ; Power management — AXP2101
    https://github.com/lewisxhe/XPowersLib.git
    ; Touch (CST9217), IMU (QMI8658), RTC (PCF85063)
    https://github.com/lewisxhe/SensorsLib.git
```

> **Note on board target:** `esp32-s3-devkitc-1` is the closest generic S3 target.
> If Waveshare publish a board definition, switch to that. The build flags above
> override flash/PSRAM settings to match the 1.75C hardware.

---

## Library Versions (pinned)

| Library          | Repo                                         | Notes                        |
|------------------|----------------------------------------------|------------------------------|
| Arduino_GFX      | moononournation/Arduino_GFX (latest main)    | CO5300 QSPI support          |
| XPowersLib       | lewisxhe/XPowersLib (latest main)            | AXP2101 power key + charging |
| SensorLib        | lewisxhe/SensorsLib (latest main)            | CST9217 touch, QMI8658 IMU   |

Pin these to specific commits once the project stabilises to avoid upstream breakage.

---

## Display Initialisation (hardware path)

The CO5300 QSPI display requires `Arduino_GFX`. Initialise inside `#ifndef WOKWI_SIM`:

```cpp
#include <Arduino_GFX_Library.h>
#include "pin_config.h"

// Bus: QSPI
Arduino_DataBus *bus = new Arduino_QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// Panel: CO5300, reset pin, rotation 0 (portrait)
Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RESET, 0 /*rotation*/);

void setupDisplay() {
    gfx->begin();
    gfx->fillScreen(BLACK);
}
```

> `Arduino_CO5300` is the driver class for the CO5300 in Arduino_GFX. Confirm the
> class name matches the installed library version — check
> `Arduino_GFX_Library/src/display/` if in doubt.

---

## Touch Initialisation (hardware path)

```cpp
#include <SensorLib.h>
#include "pin_config.h"

SensorCST9217 touch;

void setupTouch() {
    Wire.begin(IIC_SDA, IIC_SCL);
    if (!touch.begin(Wire, ADDR_CST9217, IIC_SDA, IIC_SCL)) {
        Serial.println("[TOUCH] init failed");
    }
    touch.setSwapXY(false);
    touch.setMirrorX(false);
    touch.setMirrorY(false);
}
```

Gesture reads in `Buttons::update()` (hardware path):
```cpp
if (touch.isAvailable()) {
    // check for swipe direction via start/end point delta
    // swipe left  → _clickB = true
    // swipe down  → _holdB  = true
}
```

See `docs/INPUT_DESIGN.md` for full swipe detection algorithm and thresholds.

---

## AXP2101 Power Key (hardware path)

```cpp
#include <XPowersLib.h>
#include "pin_config.h"

XPowersPMU pmu;

void setupPMU() {
    if (!pmu.begin(Wire, ADDR_AXP2101, IIC_SDA, IIC_SCL)) {
        Serial.println("[PMU] init failed");
    }
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    pmu.clearIRQ();
}

// In Buttons::update():
void pollPWRButton() {
    pmu.getIRQStatus();
    if (pmu.isPekeyShortPressIRQ()) {
        _clickA = true;
        pmu.clearIRQ();
    }
    // DO NOT handle long-press — AXP2101 manages power-off in hardware
}
```

---

## Audio Initialisation (hardware path)

ES8311 codec + I2S. Speaker amp must be enabled before playback.

```cpp
#include <driver/i2s.h>
#include "pin_config.h"

void setupAudio() {
    // 1. Configure ES8311 via I2C (Wire already initialised)
    //    Set sample rate, bit depth, master clock
    // 2. Configure I2S peripheral
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
    };
    i2s_pin_config_t pin_cfg = {
        .mck_io_num   = I2S_MCLK,
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_WS,
        .data_out_num = I2S_DO,
        .data_in_num  = I2S_DI,
    };
    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_cfg);
    // 3. Enable speaker amp
    pinMode(PA_EN, OUTPUT);
    digitalWrite(PA_EN, LOW);   // leave LOW until playback needed
}
```

During tone playback in `Tones.cpp`:
```cpp
digitalWrite(PA_EN, HIGH);   // amp on
// ... write sine buffer to I2S DMA ...
digitalWrite(PA_EN, LOW);    // amp off after playback
```

---

## Flashing

1. Connect USB-C
2. Enter download mode: hold **BOOT**, tap **RESET**, release **BOOT**
3. Device enumerates as USB serial
4. Flash: `pio run -e waveshare --target upload`
5. Monitor: `pio device monitor -e waveshare --baud 115200`

If the device doesn't enumerate, check:
- USB-C cable supports data (not charge-only)
- `ARDUINO_USB_CDC_ON_BOOT=1` is set in build flags
- Driver installed (CP210x not needed for native USB CDC; Windows may need Zadig)

---

## `config.h` Environment Awareness

`config.h` display constants are currently hardcoded for the Wokwi sim. When `[env:waveshare]`
is active, the build flags override them. Update `config.h` to use the build-flag values:

```cpp
// Display geometry — set by build flags, fallback to Wokwi sim values
#ifndef DISPLAY_WIDTH
  #define DISPLAY_WIDTH  240
  #define DISPLAY_HEIGHT 320
  #define DISPLAY_CX     120
  #define DISPLAY_CY     160
  #define ARC_OUTER_RADIUS 110
  #define ARC_INNER_RADIUS  95
#endif
```

This means the sim env works without `-D` flags and the hardware env overrides cleanly.
