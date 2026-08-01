#pragma once
#include <stdint.h>
#include "fw_version.h"

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
#define ARC_SWEEP_INTERVAL_MS   50   // Sub-second arc sweep update rate

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
#define SETTINGS_TIMEOUT_MS      8000  // auto-confirm WT page after 8s inactivity
#define TASK_SELECT_TIMEOUT_MS   3000  // auto-confirm task-select page after 3s
#define HIST_SLOTS               3     // NVS round history slots (newest → oldest)
#define HIST_TIMEOUT_MS          8000  // auto-exit history screen after 8s inactivity

// Blank the AMOLED after this long on the idle screen. Burn-in protection: the
// idle screen is nearly all static, and a timer left powered — on the bench, or
// cabled to the base station for remote development — will ghost it permanently.
// Applies to any screen _screenMaySleep() allows, not just STATE_IDLE — the
// results screen ghosts just as readily. A live round is protected in the field
// but may blank on the bench; see _screenMaySleep() in main.cpp.
#define SCREEN_SLEEP_MS        120000  // 2 minutes

// ── Flight log ────────────────────────────────────────────────────────────────
#define MAX_FLIGHTS             10

// ── OTA update status ─────────────────────────────────────────────────────────
enum OtaStatus : uint8_t {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_UP_TO_DATE,
    OTA_AVAILABLE,
    OTA_DOWNLOADING,
    OTA_SUCCESS,
    OTA_FAILED,
    OTA_NO_WIFI,
    OTA_BASE_OLDER      // base station is serving an OLDER build than we run
};

// ── OTA server (base station HTTP, same AP as timer comms) ────────────────────
#define OTA_VERSION_URL  "http://" BASE_HOST ":8080/ota/version.json"
#define OTA_FIRMWARE_URL "http://" BASE_HOST ":8080/ota/firmware.bin"

// ── Application states ────────────────────────────────────────────────────────
// How the base says the current task should be flown. Sent as `mode=` on TASK.
//
// ⚠ TARGET_PLAIN must stay the default and the fallback for anything
// unrecognised: a pre-v31 base sends no mode= at all, and a timer that guessed
// TARGET_POKER would sit waiting for a declared target that nobody is going to
// give it. Fail towards the behaviour that already exists. [TF-10]/[TF-11]
enum TargetMode : uint8_t {
    TARGET_PLAIN,      // flight time counts up; no target (every task today)
    TARGET_POKER,      // pilot declares a target; W = rest of the working time
    TARGET_LADDER      // target is the current rung; +step when reached
};

enum AppState : uint8_t {
    STATE_IDLE,
    STATE_WORKING_TIME_RUNNING,
    STATE_FLIGHT_RUNNING,
    STATE_SCRATCH_CONFIRM,
    STATE_WORKING_TIME_EXPIRED,
    STATE_SETTINGS,
    STATE_TASK_SELECT,          // settings page 2: choose F3K or F5K task type
    STATE_OTA_CHECK,            // settings page 3: check / apply OTA firmware update
    STATE_PILOT_SELECT,         // connected to base: choose pilot before each round
    STATE_COUNTDOWN,            // base sent COUNT 10..1: green arc countdown to WT start
    STATE_ALTITUDE_ENTRY,       // F5K only: enter altitude (m) after each flight
    STATE_HISTORY,              // browse last HIST_SLOTS rounds from NVS
    STATE_PREP,                 // base sent PREP t=N: yellow prep-time countdown to WT start
    STATE_LANDING,              // base sent LAND t=N: landing-window countdown after WT end
    STATE_TARGET_SET            // Poker: declare the target time (may be during a flight)
};

// ── Target tasks (Poker / Ladder) ────────────────────────────────────────────
// A target of TARGET_WINDOW_S means "the rest of the working time" — the Poker
// "end of working time" call, which the rulebook writes as W. It is resolved to a
// real number of seconds the instant it is confirmed, so a concrete time reaches
// the timekeeper, the scoring and the GliderScore export.
//
// ⚠ FAI 2025 F3K.11.5: a W call has ONLY ONE attempt, the single exception to
// re-flying a missed target until it is achieved.
#define TARGET_WINDOW_S      (-1)   // sentinel while picking; never stored as a target
#define TARGET_NONE_S          0    // no target declared
#define TARGET_PICK_SEC_STEP   5    // R click
#define TARGET_PICK_FINE_S     1    // R very-long: reach every second, so 2:38 is enterable

// What the running screen's middle label is saying, so main.cpp can choose the
// message without knowing anything about display colours.
#define TARGET_NOTE_ARMED      0    // "TGT 1:30" — what this throw is for
#define TARGET_NOTE_ACHIEVED   1
#define TARGET_NOTE_MISSED     2
#define TARGET_NOTE_WINDOW     3    // an armed W call

// ── Prep countdown (base-station driven) ─────────────────────────────────────
#define PREP_UNLOCK_S        2     // R button unlocks this many s before WT start
// Prep hit 0 but no START from base — open the window locally. The local prep
// clock is re-synced to the base every second by COUNT, so its zero is accurate
// to one packet's latency; waiting seconds for a START that may never arrive
// (dropped link) opened the window — and the WT tone — that far late.
// Just long enough for START to win the race on a healthy link.
#define PREP_START_GRACE_MS   250

// ── Base station connection state (for UI indicator) ─────────────────────────
enum BaseConnState : uint8_t {
    BASE_DISCONNECTED = 0,
    BASE_CONNECTING,
    BASE_CONNECTED
};

// ── Pilot ─────────────────────────────────────────────────────────────────────
#define MAX_PILOT_NAME  15      // "FirstName XXX" + null terminator
#define MAX_PILOTS      12

struct Pilot {
    int  id;
    char name[MAX_PILOT_NAME + 1];
};

// ── WiFi / base station ───────────────────────────────────────────────────────
// Hardcoded — closed, dedicated timer network, no security risk.
// Timer AP is run by the base station; timers connect as STA.
#define WIFI_SSID       "F3K_BASE"
#define WIFI_PASSWORD   "f3ktimer"
#define BASE_HOST       "192.168.10.1"  // base station gateway IP on timer AP
#define BASE_PORT       8765
