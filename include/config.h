#pragma once
#include <stdint.h>

// ── Working-time defaults (seconds) ──────────────────────────────────────────
#define DEFAULT_WORKING_TIME    600   // 10 minutes
#define MIN_WORKING_MINUTES       1
#define MAX_WORKING_MINUTES      15

// ── Alert timepoints (seconds remaining) ─────────────────────────────────────
static const int ALERT_TIMES[] = {30, 20, 15, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const int ALERT_COUNT   = 14;

// ── Arc colour thresholds (seconds remaining) ─────────────────────────────────
#define ARC_GREEN_THRESHOLD     60
#define ARC_ORANGE_THRESHOLD    30
#define ARC_RED_THRESHOLD       10
#define ARC_FLASH_INTERVAL_MS  250

// ── Button timings (ms) ───────────────────────────────────────────────────────
#define LONG_PRESS_MS          800

// ── Display geometry — set by build flags, fallback to Wokwi sim values ──────
#ifndef DISPLAY_WIDTH
  #define DISPLAY_WIDTH        240
  #define DISPLAY_HEIGHT       320
  #define DISPLAY_CX           120
  #define DISPLAY_CY           160
  #define ARC_OUTER_RADIUS     110
  #define ARC_INNER_RADIUS      95
#endif

// ── Scratch-confirm timeout (ms) ──────────────────────────────────────────────
#define SCRATCH_CONFIRM_MS    2000

// ── Settings ─────────────────────────────────────────────────────────────────
#define SETTINGS_TIMEOUT_MS   8000   // auto-confirm after 8s inactivity

// ── Flight log ────────────────────────────────────────────────────────────────
#define MAX_FLIGHTS             10

// ── Application states ────────────────────────────────────────────────────────
enum AppState : uint8_t {
    STATE_IDLE,
    STATE_WORKING_TIME_RUNNING,
    STATE_FLIGHT_RUNNING,
    STATE_SCRATCH_CONFIRM,
    STATE_WORKING_TIME_EXPIRED,
    STATE_SETTINGS
};
