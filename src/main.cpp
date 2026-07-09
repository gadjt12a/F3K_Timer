#include <Arduino.h>

#include "config.h"
#include "timer/WorkingTime.h"
#include "timer/FlightTimer.h"
#include "timer/FlightLog.h"
#include "display/UI.h"
#include "input/Buttons.h"
#include "audio/Tones.h"
#include "comms/TimerComms.h"

static AppState    g_state = STATE_IDLE;
static WorkingTime g_wt;
static FlightTimer g_ft;
static FlightLog   g_log;
static UI          g_ui;
static Buttons     g_btns;
static Tones       g_tones;
static TimerComms  g_comms;

static unsigned long g_scratchStartMs  = 0;
static int           g_wtMinutes       = 10;   // user-selected working time (minutes)
static unsigned long g_settingsLastMs  = 0;    // tracks inactivity for auto-confirm

// Pilot selection (only used when connected to base station)
static int  g_selectedPilotIdx = 0;
static int  g_selectedPilotId  = 0;
static char g_selectedPilotName[MAX_PILOT_NAME + 1] = "";

static void onAlert(int timeRemaining, void*) { g_tones.playAlert(timeRemaining); }

// ── Render gating ─────────────────────────────────────────────────────────────
static AppState      _lastState      = (AppState)255;
static int           _lastWtSecs     = -1;
static int           _lastWtMinutes  = -1;
static unsigned long _nextScratchMs  = 0;
static unsigned long _nextFlashMs    = 0;
static unsigned long _nextTimeMs     = 0;

static int           _lastPilotIdx    = -1;
static BaseConnState _lastConnState   = (BaseConnState)255;
static int           _lastCountdownN  = -1;
static int           g_countdownN     = 0;

static bool _needsRender(AppState state, int wtSecs, BaseConnState connState) {
    if (state != _lastState) return true;
    if (state == STATE_IDLE)              return connState != _lastConnState;
    if (state == STATE_PILOT_SELECT)      return g_selectedPilotIdx != _lastPilotIdx;
    if (state == STATE_SCRATCH_CONFIRM)   return millis() >= _nextScratchMs;
    if (state == STATE_SETTINGS)          return g_wtMinutes != _lastWtMinutes;
    if (state == STATE_WORKING_TIME_RUNNING || state == STATE_FLIGHT_RUNNING) {
        // Update every 50ms for hundredths display
        if (millis() >= _nextTimeMs) return true;
        if (wtSecs <= ARC_RED_THRESHOLD && wtSecs > 0) return millis() >= _nextFlashMs;
    }
    if (state == STATE_COUNTDOWN) return g_countdownN != _lastCountdownN;
    return false;
}

static void _doRender(AppState state, int wtSecs) {
    int battPct    = g_btns.getBatteryPercent();
    bool charging  = g_btns.isCharging();
    const char* pilot = (g_selectedPilotName[0] != '\0') ? g_selectedPilotName : nullptr;
    g_ui.render(state, g_wt, g_ft, g_log, g_scratchStartMs, g_wtMinutes,
                battPct, charging, pilot, g_comms.baseConnState(), g_countdownN);
    _lastState      = state;
    _lastWtSecs     = wtSecs;
    _lastWtMinutes  = g_wtMinutes;
    _lastConnState  = g_comms.baseConnState();
    _lastPilotIdx   = g_selectedPilotIdx;
    _lastCountdownN = g_countdownN;
    unsigned long now = millis();
    _nextScratchMs = now + 50;
    _nextFlashMs   = now + ARC_SWEEP_INTERVAL_MS;
    _nextTimeMs    = now + 10;  // 100 FPS target for smooth hundredths
}

// ─────────────────────────────────────────────────────────────────────────────
static unsigned long g_lastDbgMs = 0;

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.println("=== BOOT ===");

    // Note: Wire.begin() is called by XPowersLib in Buttons::begin() for hardware
    // to avoid double initialization. For Wokwi, Wire is not used.
    g_btns.begin();
    g_tones.begin();
    g_wt.begin(g_wtMinutes * 60);
    g_wt.setAlertCallback(onAlert);
    g_log.reset();
    g_ui.begin();

    // Start base station connection attempt in background (non-blocking)
    g_comms.begin();

    Serial.println("=== setup done ===");

    // Startup test tone — verifies audio is working
    delay(500);  // Let everything settle
    g_tones.testTone();
}

// Helper: select a pilot by index, updating name and ID globals
static void _selectPilot(int idx) {
    if (idx < 0 || idx >= g_comms.getPilotCount()) return;
    g_selectedPilotIdx = idx;
    const Pilot& p = g_comms.getPilot(idx);
    g_selectedPilotId = p.id;
    strncpy(g_selectedPilotName, p.name, MAX_PILOT_NAME);
    g_selectedPilotName[MAX_PILOT_NAME] = '\0';
}

// Helper: start a round (shared between button and base station START command)
static void _startRound(bool withFlight) {
    g_wt.reset();
    g_wt.begin(g_wtMinutes * 60);
    g_wt.start();
    g_log.reset();
    g_ft.reset();
    if (withFlight) {
        g_ft.start();
        g_state = STATE_FLIGHT_RUNNING;
    } else {
        g_state = STATE_WORKING_TIME_RUNNING;
    }
}

void loop() {
    g_btns.update();
    g_wt.update();
    g_tones.update();
    g_comms.update();

    // ── Base station commands (processed before button handling) ──────────────
    if (g_comms.hasTaskUpdate()) {
        g_wtMinutes = g_comms.getTaskWtSeconds() / 60;
        Serial.printf("[MAIN] Task update from base: %d min\n", g_wtMinutes);
    }

    if (g_comms.hasPilotList() &&
        (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT)) {
        // Fresh pilot list — go to pilot select screen
        _selectPilot(0);
        g_state = STATE_PILOT_SELECT;
    }

    if (g_comms.hasCountdown() &&
        (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT || g_state == STATE_COUNTDOWN)) {
        g_countdownN = g_comms.getCountdownN();
        g_tones.playAlert(g_countdownN);  // short beep each second
        g_state = STATE_COUNTDOWN;
    }

    if (g_comms.hasStartCommand() &&
        (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT || g_state == STATE_COUNTDOWN)) {
        if (g_state == STATE_COUNTDOWN) g_tones.playWindowOpen();
        _startRound(false);
    }

    if (g_comms.hasStopCommand() &&
        (g_state == STATE_WORKING_TIME_RUNNING || g_state == STATE_FLIGHT_RUNNING)) {
        if (g_state == STATE_FLIGHT_RUNNING) {
            unsigned long dur = g_ft.stop();
            g_log.addFlight(dur);
            g_comms.sendFlight(g_selectedPilotId, dur);
        }
        g_wt.reset();
        g_tones.silence();
        g_state = STATE_WORKING_TIME_EXPIRED;
    }

    // Button mapping for Waveshare hardware:
    // R (BOOT/right) = primary: start/stop flight
    // L (PWR/left) = secondary: scratch/settings/exit
    const bool btnR          = g_btns.btnBClicked();   // R click
    const bool btnR_held     = g_btns.btnBHeld();      // R hold (800ms)
    const bool btnR_veryLong = g_btns.btnBVeryLong();  // R very long (2s) - abort
    const bool btnL          = g_btns.btnAClicked();   // L click
    // Note: L hold triggers AXP2101 power-off, not used in software

    switch (g_state) {

        case STATE_PILOT_SELECT: {
            int count = g_comms.getPilotCount();
            if (count > 0) {
                if (btnR) {
                    _selectPilot((g_selectedPilotIdx + 1) % count);
                } else if (btnL) {
                    _selectPilot((g_selectedPilotIdx + count - 1) % count);
                }
            }
            if (btnR_held) {
                // Confirm pilot selection, return to idle ready to start
                g_state = STATE_IDLE;
            }
            break;
        }

        case STATE_COUNTDOWN:
            // Display-only state — no button handling, awaiting START from base
            break;

        case STATE_IDLE:
            if (btnR_held) {
                // R hold: go to settings (check first to avoid triggering on release)
                g_settingsLastMs = millis();
                g_state = STATE_SETTINGS;
            } else if (btnR) {
                // R click: start working time AND flight together
                _startRound(true);
            } else if (btnL) {
                // L click: start working time only (wait for pilot to launch)
                _startRound(false);
            }
            break;

        case STATE_WORKING_TIME_RUNNING:
            if (g_wt.isExpired()) { g_state = STATE_WORKING_TIME_EXPIRED; break; }
            if (btnR_veryLong) {
                // Very long R hold (2s): abort round, go to times screen
                g_wt.reset();
                g_tones.silence();
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR) {
                // R click: start a new flight (pilot launching)
                g_ft.reset();
                g_ft.start();
                g_state = STATE_FLIGHT_RUNNING;
            } else if (btnL) {
                if (g_log.count() > 0) {
                    // L click: scratch last flight (if any recorded)
                    g_scratchStartMs = millis();
                    g_state = STATE_SCRATCH_CONFIRM;
                }
                // L click with no flights: do nothing (can't scratch what doesn't exist)
            }
            break;

        case STATE_FLIGHT_RUNNING:
            if (g_wt.isExpired()) {
                unsigned long dur = g_ft.stop();
                g_log.addFlight(dur);
                g_comms.sendFlight(g_selectedPilotId, dur);
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR_veryLong) {
                // Very long R hold (2s): abort round, discard in-progress flight
                g_ft.stop();
                g_wt.reset();
                g_tones.silence();
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR) {
                // R click: stop flight and record time
                unsigned long dur = g_ft.stop();
                g_log.addFlight(dur);
                g_comms.sendFlight(g_selectedPilotId, dur);
                g_state = STATE_WORKING_TIME_RUNNING;
            }
            break;

        case STATE_SCRATCH_CONFIRM:
            if (g_wt.isExpired()) { g_state = STATE_WORKING_TIME_EXPIRED; break; }
            if (btnR_veryLong) {
                // Very long R hold: abort round
                g_wt.reset();
                g_tones.silence();
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR) {
                // R click: confirm scratch
                g_log.scratchLast();
                g_state = STATE_WORKING_TIME_RUNNING;
            } else if (millis() - g_scratchStartMs >= SCRATCH_CONFIRM_MS) {
                // Timeout: cancel scratch
                g_state = STATE_WORKING_TIME_RUNNING;
            }
            break;

        case STATE_WORKING_TIME_EXPIRED:
            // Results stay on screen until R is pressed
            if (btnR) {
                g_tones.silence();
                g_state = STATE_IDLE;
            }
            break;

        case STATE_SETTINGS: {
            bool changed = false;
            if (btnR) {
                // R click: increase time
                g_wtMinutes = min(g_wtMinutes + 1, (int)MAX_WORKING_MINUTES);
                changed = true;
            }
            if (btnL) {
                // L click: decrease time
                g_wtMinutes = max(g_wtMinutes - 1, (int)MIN_WORKING_MINUTES);
                changed = true;
            }
            if (changed) g_settingsLastMs = millis();

            // R hold or timeout: confirm and exit
            bool confirm = btnR_held ||
                           (millis() - g_settingsLastMs >= SETTINGS_TIMEOUT_MS);
            if (confirm) {
                g_state = STATE_IDLE;
            }
            break;
        }
    }

    int curWtSecs = g_wt.getRemaining();
    if (_needsRender(g_state, curWtSecs, g_comms.baseConnState())) _doRender(g_state, curWtSecs);

    unsigned long now = millis();
    if (now - g_lastDbgMs >= 2000) {
        g_lastDbgMs = now;
        Serial.printf("[DBG] state=%d  wt=%d  wtMin=%d\n",
                      (int)g_state, curWtSecs, g_wtMinutes);
    }
}
