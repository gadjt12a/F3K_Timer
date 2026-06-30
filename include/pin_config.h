#pragma once

// ============================================================================
// F3K Timer — Pin Configuration
// Waveshare ESP32-S3-Touch-AMOLED-1.75C
//
// DO NOT hardcode GPIO numbers anywhere in the codebase.
// Refer to these defines only.
//
// Wokwi simulation uses different pins — see WOKWI_SIM guards in Buttons.cpp
// and UI.cpp. The defines below are for the real hardware only.
// ============================================================================

// ── Display — CO5300 QSPI ────────────────────────────────────────────────────
#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK       38
#define LCD_CS         12
#define LCD_RESET      39
#define LCD_WIDTH     466
#define LCD_HEIGHT    466

// ── Touch — CST9217 I2C ──────────────────────────────────────────────────────
#define TP_SDA         15   // shared I2C bus
#define TP_SCL         14   // shared I2C bus
#define TP_INT         11
#define TP_RESET       40

// ── Shared I2C bus ───────────────────────────────────────────────────────────
// All peripherals below share this bus. Call Wire.begin(IIC_SDA, IIC_SCL)
// ONCE in setup(). Do not reinitialise per-peripheral.
#define IIC_SDA        15
#define IIC_SCL        14

// I2C addresses
#define ADDR_AXP2101   0x34
#define ADDR_CST9217   0x5A
#define ADDR_QMI8658   0x6B
#define ADDR_PCF85063  0x51
#define ADDR_ES8311    0x18
#define ADDR_ES7210    0x40   // dual-mic array — not used in this project

// ── Audio — ES8311 codec + I2S ───────────────────────────────────────────────
// Pin assignments from Waveshare official pin_config.h for 1.75C
// https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/blob/main/examples/Arduino-v3.3.5/libraries/Mylibrary/pin_config.h
#define I2S_MCLK       16   // was incorrectly 42
#define I2S_BCLK        9
#define I2S_WS         45
#define I2S_DO          8   // data out to speaker
#define I2S_DI         10   // data in from mic (not used)
#define PA_EN          46   // speaker amp enable — active HIGH
                            // set HIGH before audio, LOW when idle

// ── Buttons ──────────────────────────────────────────────────────────────────
// PWR: no GPIO define — read via AXP2101 power-key IRQ (XPowersLib)
//   pmu.isPekeyShortPressIRQ() after pmu.clearIRQ()
//   DO NOT intercept long-press — AXP2101 handles power-off in hardware
//
// BOOT: GPIO0 — strapping pin. Safe as short-press dev button during runtime.
//   DO NOT use for any action requiring the button held at power-on/reset.
//   Not assigned to any in-flight action.
#define BTN_BOOT        0

// Wokwi simulation buttons and buzzer (GPIO only — not present on real hardware)
#define BTN_A_SIM      16
#define BTN_B_SIM      17
#define BUZZER_SIM     13

// ── IMU — QMI8658 ────────────────────────────────────────────────────────────
// Accessed via shared I2C bus (IIC_SDA / IIC_SCL), address ADDR_QMI8658
// SensorLib: SensorQMI8658 class

// ── RTC — PCF85063 ───────────────────────────────────────────────────────────
// Accessed via shared I2C bus, address ADDR_PCF85063
// SensorLib: SensorPCF85063 class

// ── Power management — AXP2101 ───────────────────────────────────────────────
// Accessed via shared I2C bus, address ADDR_AXP2101
// XPowersLib: XPowersPMU class
// Manages: battery charging, voltage rails, power key, battery level ADC
