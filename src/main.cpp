#include <Arduino.h>

#include "config.h"
#include "timer/WorkingTime.h"
#include "timer/FlightTimer.h"
#include "timer/FlightLog.h"
#include "timer/RoundHistory.h"
#include "display/UI.h"
#include "input/Buttons.h"
#include "audio/Tones.h"
#include "comms/TimerComms.h"

#ifdef WAVESHARE_HW
#include <esp_ota_ops.h>
#include "ota/OtaUpdater.h"
#endif

static AppState      g_state = STATE_IDLE;
static WorkingTime   g_wt;
static FlightTimer   g_ft;
static FlightLog     g_log;
static RoundHistory  g_history;
static UI            g_ui;
static Buttons       g_btns;
static Tones         g_tones;
static TimerComms    g_comms;
#ifdef WAVESHARE_HW
static OtaUpdater    g_ota;
#endif

static int  g_histSlot  = 0;   // which history slot to display (0=most recent)
static HistRound g_histRound;  // loaded on demand when entering STATE_HISTORY

static unsigned long g_scratchStartMs  = 0;
static int           g_wtMinutes       = 10;   // user-selected working time (minutes)
static unsigned long g_settingsLastMs  = 0;    // tracks inactivity for auto-confirm

// F5K altitude entry (entered after Time Up screen, one flight at a time)
static int  g_altitudeM   = 0;
static int  g_altFlightNo = 0;  // 1-based index of flight being entered; 0 = not started

// Task type: false = F3K (no altitude), true = F5K (altitude entry after round)
static bool g_isF5K = false;
static unsigned long g_taskSelectLastMs = 0;
static unsigned long g_histLastMs       = 0;   // tracks inactivity in STATE_HISTORY
static bool          g_histFromSettings = false;  // true = history entered via settings chain

// ── Screen sleep (AMOLED burn-in) ────────────────────────────────────────────
// These screens are almost entirely static — GLIDE title, battery bar, timer ID,
// the results table — and this is an AMOLED, so a timer left powered will ghost
// them permanently. Blank after SCREEN_SLEEP_MS of inactivity; any button, or
// anything that moves the timer on to a different screen, brings it back.
//
// Which screens may blank depends on where the timer is: see _screenMaySleep().
// The base station can also force the screen on for a fixed window (SCREEN t=N)
// so display work can be checked without standing over the device.
static unsigned long g_lastActivityMs     = 0;
static bool          g_screenAsleep       = false;
static unsigned long g_screenForceUntilMs = 0;  // base station override deadline
static void _wakeScreen();            // defined below _lastState, which it resets

// Pilot selection (only used when connected to base station)
static int  g_selectedPilotIdx = 0;
static int  g_selectedPilotId  = 0;
static char g_selectedPilotName[MAX_PILOT_NAME + 1] = "";

// Base-driven prep / landing countdowns (timer counts locally, COUNT re-syncs prep)
static unsigned long g_prepEndMs   = 0;  // millis() deadline for prep = 0
static unsigned long g_prepZeroMs  = 0;  // when prep first hit 0 (for no-START fallback)
static int  g_prepTotalS  = 0;           // arc denominator (original prep duration)
static int  g_prepDispDs  = 0;           // currently displayed prep time, tenths of a second
static int  g_prepBeepS   = 0;           // last prep second beeped (avoid repeats)
static unsigned long g_landEndMs   = 0;
static int  g_landTotalS  = 0;
static int  g_landDispS   = 0;
static bool g_earlyFlight = false;       // flight started during final 2s of prep
static bool g_jumpedStart = false;       // flight launched before the WT long beep — invalid

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
static int           _lastAltitudeM   = -1;
static int           _lastAltFlightNo = -1;
static bool          _lastIsF5K       = false;
static int           _lastTimerId     = -2;  // -2 = "never rendered"
static int           _lastHistSlot    = -1;
static int           _lastPrepDispDs  = -1;
static int           _lastLandDispS   = -1;
#ifdef WAVESHARE_HW
static OtaStatus     _lastOtaStatus   = OTA_IDLE;
static int           _lastOtaProg10   = -1;  // progress in 10% increments
#endif

static void _wakeScreen() {
    g_lastActivityMs = millis();
    if (g_screenAsleep) {
        g_screenAsleep = false;
        _lastState = (AppState)255;   // force a full repaint, not a diffed update
    }
}

// True while a round is actually being flown, i.e. someone is watching the clock.
static bool _roundLive(AppState s) {
    switch (s) {
        case STATE_PREP:
        case STATE_COUNTDOWN:
        case STATE_WORKING_TIME_RUNNING:
        case STATE_FLIGHT_RUNNING:
        case STATE_SCRATCH_CONFIRM:
        case STATE_LANDING:
            return true;
        default:
            return false;
    }
}

// Bench mode: a USB cable is attached, so this timer is on a desk or wired to
// the base station for development — not in a caller's hand at a competition.
// The distinction matters because the two want opposite things: in the field the
// screen must never blank mid-round, while on the bench a simulated round can run
// for ten minutes with nobody looking, which is exactly what burns an AMOLED.
static bool _benchMode() {
    // VBUS is the real signal, but it is read through the AXP2101, which does
    // not always survive a reset-during-boot. The USB CDC host connection is an
    // independent fallback and is true precisely when a serial monitor is
    // attached — the case we most want covered.
    return g_btns.isUsbPowered() || (bool)Serial;
}

// Whether this screen may blank after the inactivity period.
//
// The first version blanked STATE_IDLE only. Testing a full round from the base
// station showed the timer parks on the *results* screen afterwards and sits
// there fully lit until someone presses R — so the burn-in it was written to
// prevent was still happening, just on a different screen. Settings, pilot
// select, history and the (now timeout-free) OTA screen had the same hole.
static bool _screenMaySleep(AppState s) {
    if (!_roundLive(s)) return true;
    // On the bench, even a live round may blank: nobody is holding it.
    return _benchMode();
}

static bool _needsRender(AppState state, int wtSecs, BaseConnState connState) {
    if (state != _lastState) return true;
    if (state == STATE_IDLE)
        return connState != _lastConnState || g_comms.getTimerId() != _lastTimerId;
    if (state == STATE_HISTORY)       return g_histSlot != _lastHistSlot;
#ifdef WAVESHARE_HW
    if (state == STATE_OTA_CHECK)
        return (g_ota.getStatus() != _lastOtaStatus ||
                g_ota.getProgress() / 10 != _lastOtaProg10);
#endif
    if (state == STATE_PILOT_SELECT)  return g_selectedPilotIdx != _lastPilotIdx;
    if (state == STATE_SCRATCH_CONFIRM)   return millis() >= _nextScratchMs;
    if (state == STATE_SETTINGS)          return g_wtMinutes != _lastWtMinutes;
    if (state == STATE_TASK_SELECT)       return g_isF5K != _lastIsF5K;
    if (state == STATE_WORKING_TIME_RUNNING || state == STATE_FLIGHT_RUNNING) {
        // Update every 50ms for hundredths display
        if (millis() >= _nextTimeMs) return true;
        if (wtSecs <= ARC_RED_THRESHOLD && wtSecs > 0) return millis() >= _nextFlashMs;
    }
    if (state == STATE_COUNTDOWN)      return g_countdownN != _lastCountdownN;
    if (state == STATE_PREP)           return g_prepDispDs != _lastPrepDispDs;
    if (state == STATE_LANDING)        return g_landDispS  != _lastLandDispS;
    if (state == STATE_ALTITUDE_ENTRY) return g_altitudeM != _lastAltitudeM || g_altFlightNo != _lastAltFlightNo;
    return false;
}

static void _doRender(AppState state, int wtSecs) {
    // History screen is rendered via a separate path (passes HistRound data)
    if (state == STATE_HISTORY) {
        g_history.load(g_histSlot, g_histRound);
        g_ui.renderHistory(g_histSlot, g_histRound, HIST_SLOTS);
        _lastState    = state;
        _lastHistSlot = g_histSlot;
        return;
    }
#ifdef WAVESHARE_HW
    if (state == STATE_OTA_CHECK) {
        g_ui.renderOtaCheck(g_ota.getStatus(), g_ota.getProgress(), g_ota.getAvailableVersion());
        _lastOtaStatus = g_ota.getStatus();
        _lastOtaProg10 = g_ota.getProgress() / 10;
        _lastState     = state;
        return;
    }
#endif

    int battPct    = g_btns.getBatteryPercent();
    bool charging  = g_btns.isCharging();
    const char* pilot = (g_selectedPilotName[0] != '\0') ? g_selectedPilotName : nullptr;
    // Aux countdowns are passed in tenths so the prep clock can show them; landing
    // still only tracks whole seconds, so scale it up to the same units.
    int auxRemainDs = (state == STATE_LANDING) ? g_landDispS * 10 : g_prepDispDs;
    int auxTotalDs  = ((state == STATE_LANDING) ? g_landTotalS : g_prepTotalS) * 10;
    g_ui.render(state, g_wt, g_ft, g_log, g_scratchStartMs, g_wtMinutes,
                battPct, charging, pilot, g_comms.baseConnState(), g_countdownN,
                g_altitudeM, g_altFlightNo, g_log.count(), g_isF5K,
                g_comms.getTimerId(), auxRemainDs, auxTotalDs, g_jumpedStart);
    _lastState      = state;
    _lastWtSecs     = wtSecs;
    _lastWtMinutes  = g_wtMinutes;
    _lastConnState  = g_comms.baseConnState();
    _lastPilotIdx   = g_selectedPilotIdx;
    _lastCountdownN  = g_countdownN;
    _lastAltitudeM   = g_altitudeM;
    _lastAltFlightNo = g_altFlightNo;
    _lastIsF5K       = g_isF5K;
    _lastTimerId     = g_comms.getTimerId();
    _lastPrepDispDs  = g_prepDispDs;
    _lastLandDispS   = g_landDispS;
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
    g_history.begin();

#ifdef WAVESHARE_HW
    // Mark this OTA image valid now that display and NVS are confirmed working.
    // Must happen before any peripheral that could fail to avoid a rollback loop.
    {
        esp_ota_img_states_t ota_state;
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
            ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("=== OTA: app marked valid ===");
        }
    }
    g_ota.begin();
#endif

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

// Helper: start a round (shared between button and base station START command).
// preserveFlight: a flight is already running (early launch during prep) — keep it.
static void _startRound(bool withFlight, bool preserveFlight = false) {
    g_wt.reset();
    g_wt.begin(g_wtMinutes * 60);
    g_wt.start();
    g_log.reset();
    if (!preserveFlight) g_ft.reset();
    g_altFlightNo = 0;
    g_altitudeM   = 0;
    g_history.startRound(g_isF5K, g_selectedPilotName);
    if (withFlight) {
        if (!preserveFlight) g_ft.start();
        g_state = STATE_FLIGHT_RUNNING;
    } else {
        g_state = STATE_WORKING_TIME_RUNNING;
    }
}

// Helper: stop the running flight and record it. A jumped start (launched before
// the WT long beep) is logged but immediately scratched — invalid flight — and is
// NOT sent to the base station or NVS round history.
static void _recordFlight() {
    unsigned long dur = g_ft.stop();
    g_log.addFlight(dur);
    if (g_jumpedStart) {
        g_log.scratchLast();
        g_jumpedStart = false;
        g_comms.sendJumped(g_selectedPilotId, dur);  // CD note only — never scored
        Serial.printf("[MAIN] Jumped start — flight %.2fs invalidated\n", dur / 1000.0f);
    } else {
        g_history.recordFlight(dur);
        g_comms.sendFlight(g_selectedPilotId, dur);
    }
}

// Re-report the finished round so anything lost on the way to the base gets a
// second chance. Sourced from NVS rather than the live FlightLog on purpose:
// RoundHistory holds exactly the valid, scoreable flights (a jumped start goes to
// the log scratched but never to history) and it survives a reboot mid-round.
static void _reconcileRound() {
    if (g_selectedPilotId <= 0) return;
    HistRound r;
    if (!g_history.load(0, r) || !r.valid || r.count == 0) return;
    // Never report altitudes on an F3K round — it has none, so anything here can
    // only be stale. [I-28] fixes the NVS leak that produced them; this is the
    // second line of defence, because a bogus altitude silently corrupts scoring.
    const bool isF5K = (r.discipline[1] == '5');
    g_comms.resendRound(g_selectedPilotId, r.flightMs,
                        isF5K ? r.altitudeM : nullptr, r.count);
}

void loop() {
    g_btns.update();
    g_wt.update();
    g_tones.update();
    g_comms.update();

    // ── Base station commands (processed before button handling) ──────────────
    if (g_comms.hasTaskUpdate()) {
        g_wtMinutes = g_comms.getTaskWtSeconds() / 60;
        g_isF5K     = g_comms.isF5K();
        Serial.printf("[MAIN] Task update from base: %d min, %s\n", g_wtMinutes, g_isF5K ? "F5K" : "F3K");
    }

    if (g_comms.hasPilotList() &&
        (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT ||
         g_state == STATE_WORKING_TIME_EXPIRED || g_state == STATE_ALTITUDE_ENTRY)) {
        // Fresh pilot list — go to pilot select screen (accept from any inactive state)
        _selectPilot(0);
        g_state = STATE_PILOT_SELECT;
    }

    // PREP t=N — base started (or skipped within) the preparation countdown
    if (g_comms.hasPrepStart()) {
        int t = g_comms.getPrepSeconds();
        if (g_state == STATE_PREP) {
            // Skip/re-sync mid-prep: jump the deadline, keep the original arc total
            g_prepEndMs = millis() + (unsigned long)t * 1000UL;
            g_prepBeepS = t + 1;
            g_prepZeroMs = 0;
        } else if (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT ||
                   g_state == STATE_COUNTDOWN || g_state == STATE_WORKING_TIME_EXPIRED ||
                   g_state == STATE_LANDING) {
            g_prepEndMs   = millis() + (unsigned long)t * 1000UL;
            g_prepTotalS  = t;
            g_prepBeepS   = t + 1;
            g_prepZeroMs  = 0;
            g_earlyFlight = false;
            g_jumpedStart = false;
            g_ft.reset();
            g_state = STATE_PREP;
            Serial.printf("[MAIN] Prep countdown started: %ds\n", t);
        }
    }

    if (g_comms.hasCountdown()) {
        int n = g_comms.getCountdownN();
        if (g_state == STATE_PREP) {
            // Base tick for the last 10s — re-sync the local prep clock to it
            g_prepEndMs = millis() + (unsigned long)n * 1000UL;
        } else if (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT ||
                   g_state == STATE_COUNTDOWN) {
            // Fallback path (no PREP received, e.g. connected mid-prep on old base)
            g_countdownN = n;
            g_tones.playAlert(g_countdownN);  // short beep each second
            g_state = STATE_COUNTDOWN;
        }
    }

    if (g_comms.hasStartCommand() &&
        (g_state == STATE_IDLE || g_state == STATE_PILOT_SELECT ||
         g_state == STATE_COUNTDOWN || g_state == STATE_PREP)) {
        if (g_state == STATE_COUNTDOWN || g_state == STATE_PREP) g_tones.playWindowOpen();
        // An early launch during prep (last 2s) keeps its running flight timer
        _startRound(g_earlyFlight, g_earlyFlight);
        g_earlyFlight = false;
    }

    if (g_comms.hasStopCommand()) {
        if (g_state == STATE_WORKING_TIME_RUNNING || g_state == STATE_FLIGHT_RUNNING) {
            if (g_state == STATE_FLIGHT_RUNNING) _recordFlight();
            g_wt.reset();
            g_tones.silence();
            g_state = STATE_WORKING_TIME_EXPIRED;
        } else if (g_state == STATE_PREP || g_state == STATE_COUNTDOWN ||
                   g_state == STATE_LANDING) {
            // CD aborted the round. Anything already flown still has to be shown
            // to the caller and — on F5K — have its altitudes entered, so only
            // drop straight to IDLE when nothing was recorded. Aborting from the
            // landing window used to bin the whole round silently.
            g_ft.stop();
            g_wt.reset();
            g_tones.silence();
            g_earlyFlight = false;
            g_jumpedStart = false;
            g_state = (g_log.count() > 0) ? STATE_WORKING_TIME_EXPIRED : STATE_IDLE;
        }
    }

    // LAND t=N — landing window countdown after WT end (STOP always precedes it)
    if (g_comms.hasLandStart() &&
        (g_state == STATE_WORKING_TIME_EXPIRED || g_state == STATE_IDLE)) {
        g_landTotalS = g_comms.getLandSeconds();
        g_landEndMs  = millis() + (unsigned long)g_landTotalS * 1000UL;
        g_landDispS  = g_landTotalS;
        g_state = STATE_LANDING;
        Serial.printf("[MAIN] Landing window: %ds\n", g_landTotalS);
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
                g_comms.sendSelect(g_selectedPilotId);
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
            } else if (g_comms.isConnected()) {
                // Connected to base: rounds are started by the base station only.
                // WT start (L) and WT+flight start (R) are locked.
            } else if (btnR) {
                // R click: start working time AND flight together
                _startRound(true);
            } else if (btnL) {
                // L click: start working time only (wait for pilot to launch)
                _startRound(false);
            }
            break;

        case STATE_PREP: {
            unsigned long nowP = millis();
            long remMs = (long)(g_prepEndMs - nowP);
            int  remDs = (remMs > 0) ? (int)((remMs + 99) / 100) : 0;   // tenths, rounded up
            int  rem   = (remDs + 9) / 10;                              // whole seconds
            g_prepDispDs = remDs;
            // Beeps at 30, 15, 10..1; the long beep comes with START (window open)
            if (rem != g_prepBeepS && rem > 0 &&
                (rem == 30 || rem == 15 || rem <= 10)) {
                g_tones.playAlert(rem);
                g_prepBeepS = rem;
            }
            // R unlocks for the final PREP_UNLOCK_S seconds. Launching before the
            // WT long beep is a jumped start — the flight runs but is invalidated
            // when stopped.
            //
            // rem == 0 must stay unlocked: the local clock hits zero up to a second
            // before START lands (base sends COUNT 1, sleeps 1s, then TASK+START),
            // and that dead zone is exactly when a pilot jumps. Requiring rem > 0
            // silently ate the press — btnBClicked() is one-shot — so the round then
            // opened on WAIT with no flight running.
            if (btnR && !g_earlyFlight && rem <= PREP_UNLOCK_S) {
                g_ft.reset();
                g_ft.start();
                g_earlyFlight = true;
                g_jumpedStart = true;
                Serial.printf("[MAIN] Flight started during prep (rem=%ds) — JUMPED START\n", rem);
            } else if (btnR) {
                Serial.printf("[MAIN] R ignored during prep — %ds remaining (unlocks at %ds)\n",
                              rem, (int)PREP_UNLOCK_S);
            }
            // Fallback: prep hit 0 but no START from base (packet loss) — start locally
            if (rem == 0) {
                if (g_prepZeroMs == 0) {
                    g_prepZeroMs = nowP;
                } else if (nowP - g_prepZeroMs >= PREP_START_GRACE_MS) {
                    Serial.println("[MAIN] No START after prep end — starting round locally");
                    g_tones.playWindowOpen();
                    _startRound(g_earlyFlight, g_earlyFlight);
                    g_earlyFlight = false;
                }
            }
            break;
        }

        case STATE_LANDING: {
            long remMs = (long)(g_landEndMs - millis());
            int  rem   = (remMs > 0) ? (int)((remMs + 999) / 1000) : 0;
            g_landDispS = rem;
            // R click skips ahead to the results screen
            if (rem == 0 || btnR) g_state = STATE_WORKING_TIME_EXPIRED;
            break;
        }

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
                _recordFlight();
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR_veryLong) {
                // Very long R hold (2s): abort round, discard in-progress flight
                g_ft.stop();
                g_wt.reset();
                g_tones.silence();
                g_jumpedStart = false;
                g_state = STATE_WORKING_TIME_EXPIRED;
                break;
            }
            if (btnR) {
                // R click: stop flight and record time
                _recordFlight();
                g_state = STATE_WORKING_TIME_RUNNING;
            }
            break;

        case STATE_ALTITUDE_ENTRY: {
            if (btnR_held) {
                // Confirm altitude for this flight
                g_history.recordAltitude(g_altFlightNo, g_altitudeM);
                g_comms.sendAltitude(g_selectedPilotId, g_altFlightNo, g_altitudeM);
                if (g_altFlightNo < g_log.count()) {
                    // Advance to next flight
                    g_altFlightNo++;
                    g_altitudeM = 0;
                    // Stay in STATE_ALTITUDE_ENTRY — _needsRender detects flightNo change
                } else {
                    // Second reconcile pass, and the only one that can carry
                    // altitudes: the pass at WORKING_TIME_EXPIRED runs before any
                    // altitude has been entered. Must come before the pilot binding
                    // is cleared, or there is nothing left to attribute it to.
                    _reconcileRound();
                    g_selectedPilotName[0] = '\0';
                    g_selectedPilotId = 0;
                    g_state = STATE_IDLE;
                }
                break;
            }
            if (btnR) {
                // Ones digit cycles 0→9→0; no-op at 100 (can't have 101m)
                if (g_altitudeM < 100) {
                    g_altitudeM = (g_altitudeM % 10 == 9) ? g_altitudeM - 9 : g_altitudeM + 1;
                }
            } else if (btnL) {
                // Tens digit cycles 0→10→...→100→0; ones digit preserved (capped at 100)
                int ones = g_altitudeM % 10;
                int tens = (g_altitudeM / 10) * 10;
                tens = (tens >= 100) ? 0 : tens + 10;
                g_altitudeM = min(tens + ones, 100);
            }
            break;
        }

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
            // Results stay on screen; R advances to altitude entry (F5K) or IDLE
            if (btnR) {
                g_tones.silence();
                if (g_isF5K && g_log.count() > 0) {
                    g_altFlightNo = 1;
                    g_altitudeM   = 0;
                    g_state = STATE_ALTITUDE_ENTRY;
                } else {
                    g_selectedPilotName[0] = '\0';
                    g_selectedPilotId = 0;
                    g_state = STATE_IDLE;
                }
            } else if (btnL) {
                // L click: browse round history from NVS (start at most recent)
                g_histSlot  = 0;
                g_histLastMs = millis();
                g_state = STATE_HISTORY;
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

            // R hold or timeout: save WT and advance to task-type page
            bool confirm = btnR_held ||
                           (millis() - g_settingsLastMs >= SETTINGS_TIMEOUT_MS);
            if (confirm) {
                g_taskSelectLastMs = millis();
                g_state = STATE_TASK_SELECT;
            }
            break;
        }

        case STATE_TASK_SELECT: {
            bool changed = false;
            if (btnR || btnL) {
                g_isF5K = !g_isF5K;
                changed = true;
            }
            if (changed) g_taskSelectLastMs = millis();

            // R hold or 3s timeout: advance to round history (page 3); OTA is last (page 4)
            bool confirm = btnR_held ||
                           (millis() - g_taskSelectLastMs >= TASK_SELECT_TIMEOUT_MS);
            if (confirm) {
                g_histSlot        = 0;
                g_histLastMs      = millis();
                g_histFromSettings = true;
                g_state = STATE_HISTORY;
            }
            break;
        }

        case STATE_OTA_CHECK: {
#ifdef WAVESHARE_HW
            OtaStatus ota = g_ota.getStatus();
            // No inactivity timeout here: R is the only way out.
            //
            // There used to be an 8s one, and OTA_CHECKING was not in the `busy`
            // set below — so the screen bounced back to IDLE while the version
            // request to the base station was still in flight, and the check
            // never got to finish. A timeout is wrong for this screen anyway:
            // fetching and flashing are slow by nature, and the user is standing
            // there watching it. OTA_SUCCESS calls ESP.restart(), so `busy`
            // cannot strand anyone.
            bool busy = (ota == OTA_DOWNLOADING || ota == OTA_SUCCESS);
            if (!busy) {
                if (btnR_held && ota == OTA_AVAILABLE) {
                    g_ota.startUpdate();
                } else if (btnR) {
                    g_state = STATE_IDLE;
                    break;
                }
            }
#else
            g_state = STATE_IDLE;
#endif
            break;
        }

        case STATE_HISTORY: {
            auto _exitHistory = [&]() {
#ifdef WAVESHARE_HW
                if (g_histFromSettings) {
                    g_histFromSettings = false;
                    g_ota.check();
                    g_state = STATE_OTA_CHECK;
                    return;
                }
#endif
                g_histFromSettings = false;
                g_state = STATE_IDLE;
            };
            bool acted = false;
            if (btnR_held) {
                _exitHistory();
                break;
            } else if (btnR) {
                if (g_histSlot == 0) {
                    _exitHistory();
                    break;
                }
                g_histSlot--;
                acted = true;
            } else if (btnL) {
                if (g_histSlot < HIST_SLOTS - 1) g_histSlot++;
                acted = true;
            }
            if (acted) g_histLastMs = millis();
            // 8s inactivity timeout
            if (millis() - g_histLastMs >= HIST_TIMEOUT_MS) _exitHistory();
            break;
        }
    }

    // ── Screen sleep (AMOLED burn-in) ────────────────────────────────────────
    // Any press is activity, whether or not the current state acted on it.
    if (btnR || btnL || btnR_held || btnR_veryLong) _wakeScreen();
    // In the field a live round keeps the screen up unconditionally — the caller
    // must never look down mid-flight at a black display and have to press
    // something. On the bench that protection is dropped (see _screenMaySleep).
    if (!_screenMaySleep(g_state)) _wakeScreen();
    // Base station asked for the screen, so display work can be checked remotely
    // without someone standing over the timer.
    if (g_comms.hasScreenCmd()) {
        int secs = g_comms.getScreenSeconds();
        g_screenForceUntilMs = secs > 0 ? millis() + (unsigned long)secs * 1000UL : 0;
        _wakeScreen();
        Serial.printf("[MAIN] Screen forced on for %ds (0 = released)\n", secs);
    }
    if (g_screenForceUntilMs && (long)(millis() - g_screenForceUntilMs) < 0) _wakeScreen();
#ifdef WAVESHARE_HW
    // An OTA check or download is progress the user is watching, with no button
    // presses to keep it alive. Treat it as activity in its own right.
    if (g_state == STATE_OTA_CHECK &&
        (g_ota.getStatus() == OTA_CHECKING || g_ota.getStatus() == OTA_DOWNLOADING)) {
        _wakeScreen();
    }
#endif

    // Reconcile on the transition into the results screen, wherever it came from.
    // Nine separate paths reach WORKING_TIME_EXPIRED — WT expiry, CD STOP, abort
    // from prep/countdown/landing, scratch-confirm timeout — so hooking the
    // transition rather than each call site means a path added later cannot
    // silently skip the safety net.
    static AppState s_prevState = STATE_IDLE;
    if (g_state != s_prevState) {
        if (g_state == STATE_WORKING_TIME_EXPIRED) _reconcileRound();
        s_prevState = g_state;
    }

    int curWtSecs = g_wt.getRemaining();
    if (!g_screenAsleep && _screenMaySleep(g_state) &&
            millis() - g_lastActivityMs >= SCREEN_SLEEP_MS) {
        g_screenAsleep = true;
        g_ui.blank();
        Serial.printf("[MAIN] Screen asleep (state=%d) — press any button to wake\n",
                      (int)g_state);
    }
    if (g_screenAsleep) {
        // Skip rendering entirely; the panel stays black until something wakes it
    } else if (_needsRender(g_state, curWtSecs, g_comms.baseConnState())) {
        _doRender(g_state, curWtSecs);
    }

    unsigned long now = millis();
    if (now - g_lastDbgMs >= 2000) {
        g_lastDbgMs = now;
        Serial.printf("[DBG] state=%d  wt=%d  wtMin=%d\n",
                      (int)g_state, curWtSecs, g_wtMinutes);
    }
}
