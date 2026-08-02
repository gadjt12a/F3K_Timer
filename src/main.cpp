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

// ── Target tasks: Poker and Ladder ───────────────────────────────────────────
// One mechanism, two sources for the target: the pilot's call (Poker) or the
// current rung (Ladder). Both count the flight DOWN to the target and re-fly the
// same target on a miss. [TF-10]/[TF-11]
static int  g_targetS       = TARGET_NONE_S;  // armed target, seconds; 0 = none
static bool g_targetWindow  = false;          // armed target was a W ("end of working time")
static bool g_windowUsed    = false;          // a W has been called this round — one attempt only
static int  g_targetsScored = 0;              // achieved targets so far (Poker: max 3)
// Picker scratch state, committed to g_target* only on confirm.
static int  g_pickMin       = 0;
static int  g_pickSec       = 0;
static bool g_pickWindow    = false;
static bool g_pickNone      = true;           // the "---" position: confirm to clear
static AppState g_pickReturnState = STATE_WORKING_TIME_RUNNING;
static bool g_prepDeclared  = false;          // call made in prep; survives _startRound()
// A W call that has been made but not yet thrown. ⚠ W must NOT resolve to a number
// when it is confirmed: it means "I will still be flying when the window closes",
// so the seconds it is worth are the seconds left at the LAUNCH, not at the call.
// Resolving at confirm made a W unflyable unless the pilot threw instantly — the
// clock kept running while they walked to the line and the target never moved.
static bool g_targetWindowPending = false;
// This flight was still in the air when the working window closed. That IS the W
// call being achieved, and it is judged on the fact rather than on the arithmetic:
// a flight recorded at expiry can land a few tens of ms short of its resolved
// target and truncate to one second under it.
static bool g_flightEndedByExpiry = false;
// Outcome flash on the working-time screen, instead of a screen to dismiss —
// there is no room for one in a turn-around.
static unsigned long g_targetMsgUntilMs = 0;
static bool          g_targetMsgHit     = false;

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
static int           _lastPickMin     = -1;
static int           _lastPickSec     = -1;
static bool          _lastPickWindow  = false;
static bool          _lastPickNone    = false;
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
        // The target picker is reached mid-round, usually with a flight in the
        // air. Blanking it would be the worst possible moment. [TF-10]
        case STATE_TARGET_SET:
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
    // ⚠ A live round with the base station driving it is a REAL round, cable or
    // not. The timer is wired to the Pi for development, so bench mode is true for
    // every round we actually test — and the screen blanked mid-flight, which is
    // the one moment it must not. Kris, watching it happen: "we can't have that."
    //
    // The AMOLED protection this weakens only mattered for an unattended simulated
    // round, and a round the base station is running is by definition attended.
    if (g_comms.isConnected()) return false;
    // Standalone on the bench: nobody is holding it, so let it blank.
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
    if (state == STATE_TARGET_SET) {
        // The dialled value, and the running flight clock ticking beside it —
        // the flight is usually in the air while this screen is up. Rendered on
        // the same cadence as the running screen so the two agree.
        if (g_pickMin != _lastPickMin || g_pickSec != _lastPickSec ||
            g_pickWindow != _lastPickWindow || g_pickNone != _lastPickNone) return true;
        return millis() >= _nextTimeMs;
    }
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
        // Sample once. _status is volatile and written by the check task on
        // another core, while this full-screen clear+draw+flush takes tens of ms
        // — longer than a check takes to complete (~30 ms, measured). Reading it
        // again after drawing recorded a status that was never rendered, so
        // _needsRender() then compared equal and never repainted: the screen sat
        // on CHECKING forever while the status had long since resolved. [I-40]
        OtaStatus   otaStatus = g_ota.getStatus();
        int         otaProg   = g_ota.getProgress();
        g_ui.renderOtaCheck(otaStatus, otaProg, g_ota.getAvailableVersion());
        _lastOtaStatus = otaStatus;
        _lastOtaProg10 = otaProg / 10;
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
    // The middle label on the running screen. Precedence matters: the outcome
    // flash beats the armed target, because the caller has just landed and needs
    // to know whether the call stood before thinking about the next one. There is
    // no outcome screen — it would have to be dismissed, and in a turn-around
    // there is no room for that. [TF-10]/[TF-11]
    static char  targetBuf[16];
    const char*  targetNote     = nullptr;
    int          targetNoteKind = TARGET_NOTE_ARMED;
    if (millis() < g_targetMsgUntilMs) {
        targetNote     = g_targetMsgHit ? "ACHIEVED" : "MISSED";
        targetNoteKind = g_targetMsgHit ? TARGET_NOTE_ACHIEVED : TARGET_NOTE_MISSED;
    } else if (g_targetWindowPending) {
        // Called but not yet thrown, so it has no number yet. Show what it is
        // worth if they launch now — it shrinks as they walk to the line, which is
        // exactly the thing the timekeeper needs to see.
        const int rem = g_wt.getRemaining();
        snprintf(targetBuf, sizeof(targetBuf), "TGT W %d:%02d", rem / 60, rem % 60);
        targetNote     = targetBuf;
        targetNoteKind = TARGET_NOTE_WINDOW;
    } else if (g_targetS > TARGET_NONE_S && !g_ft.isRunning()) {
        // Only when not flying: while airborne the big figure is already counting
        // down to this target, so repeating it here would be noise.
        snprintf(targetBuf, sizeof(targetBuf), "TGT %d:%02d",
                 g_targetS / 60, g_targetS % 60);
        targetNote     = targetBuf;
        targetNoteKind = g_targetWindow ? TARGET_NOTE_WINDOW : TARGET_NOTE_ARMED;
    }

    g_ui.render(state, g_wt, g_ft, g_log, g_scratchStartMs, g_wtMinutes,
                battPct, charging, pilot, g_comms.baseConnState(), g_countdownN,
                g_altitudeM, g_altFlightNo, g_log.count(), g_isF5K,
                g_comms.getTimerId(), auxRemainDs, auxTotalDs, g_jumpedStart,
                g_targetS, g_targetWindow,
                g_pickMin, g_pickSec, g_pickWindow, g_pickNone,
                targetNote, targetNoteKind);
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
    _lastPickMin     = g_pickMin;
    _lastPickSec     = g_pickSec;
    _lastPickWindow  = g_pickWindow;
    _lastPickNone    = g_pickNone;
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
static void _resolveWindowTarget();   // defined below; needed by _startRound()

static void _startRound(bool withFlight, bool preserveFlight = false) {
    g_wt.reset();
    g_wt.begin(g_wtMinutes * 60);
    g_wt.start();
    g_log.reset();
    if (!preserveFlight) g_ft.reset();
    g_altFlightNo = 0;
    g_altitudeM   = 0;
    // Arm the target for the new round. A ladder starts on rung 1 and the pilot
    // must know it before they throw; Poker starts with nothing declared.
    // ⚠ A timer assigned to a pilot late also starts on rung 1 (Kris) — it falls
    // out of this, because a late timer runs _startRound() like any other. [TF-02]
    g_targetsScored = 0;
    if (g_comms.getTargetMode() == TARGET_LADDER) {
        // Rung 1: the first of an explicit list (K, M) or the stepped start (D).
        g_targetS             = g_comms.getLadderRungCount() > 0
                                    ? g_comms.getLadderRungS(0)
                                    : g_comms.getLadderStartS();
        g_targetWindow        = false;
        g_targetWindowPending = false;
        g_windowUsed          = false;
    } else if (g_prepDeclared && g_targetS > TARGET_NONE_S) {
        // ⚠ A Poker call made during prep must survive the window opening. This
        // reset used to clear it unconditionally, so declaring early — the whole
        // point of allowing it in prep — silently threw the call away.
        Serial.printf("[MAIN] Prep-declared target kept: %d:%02d\n",
                      g_targetS / 60, g_targetS % 60);
    } else if (g_prepDeclared && g_targetWindowPending) {
        // A W called in prep, still waiting for the launch to give it a number.
        // ⚠ g_windowUsed must survive with it, or the one-attempt rule is lost.
        Serial.println("[MAIN] Prep-declared W kept, unresolved until launch");
    } else {
        g_targetS             = TARGET_NONE_S;
        g_targetWindow        = false;
        g_targetWindowPending = false;
        g_windowUsed          = false;
    }
    g_prepDeclared  = false;
    g_history.startRound(g_isF5K, g_selectedPilotName);
    if (withFlight) {
        if (!preserveFlight) g_ft.start();
        g_flightEndedByExpiry = false;
        _resolveWindowTarget();   // the window has opened and the glider is away
        g_state = STATE_FLIGHT_RUNNING;
    } else {
        g_state = STATE_WORKING_TIME_RUNNING;
    }
}

// Helper: stop the running flight and record it. A jumped start (launched before
// the WT long beep) is logged but immediately scratched — invalid flight — and is
// NOT sent to the base station or NVS round history.
// May the timekeeper declare a target right now? Poker only, and only when none
// is armed — FAI: a call that was not reached "cannot be changed", so an armed
// target is not up for revision until it has been flown. [TF-10]
static bool _pokerCanDeclare() {
    return g_comms.getTargetMode() == TARGET_POKER && g_targetS <= TARGET_NONE_S &&
           !g_targetWindowPending;   // a called-but-unthrown W is armed, not absent
}

// A W becomes a number the moment the glider leaves the hand: "the rest of the
// working time" is measured from the launch. Called from every path that starts a
// flight. No-op unless a W is waiting to be resolved.
static void _resolveWindowTarget() {
    if (!g_targetWindowPending) return;
    g_targetWindowPending = false;
    g_targetS      = g_wt.getRemaining();
    g_targetWindow = true;
    Serial.printf("[MAIN] W resolved at launch: %ds to the end of the window\n", g_targetS);
}

// The prep countdown, its beeps and the no-START fallback. Extracted because the
// caller may be in the target picker when prep runs out: the clock must not stop
// just because a different screen is up, or the beeps go silent and the window
// never opens. Returns whole seconds remaining. ⚠ May start the round, so callers
// must re-check g_state afterwards.
static int _tickPrepClock() {
    const unsigned long nowP = millis();
    const long remMs = (long)(g_prepEndMs - nowP);
    const int  remDs = (remMs > 0) ? (int)((remMs + 99) / 100) : 0;   // tenths, rounded up
    const int  rem   = (remDs + 9) / 10;                              // whole seconds
    g_prepDispDs = remDs;
    // Beeps at 30, 15, 10..1; the long beep comes with START (window open)
    if (rem != g_prepBeepS && rem > 0 && (rem == 30 || rem == 15 || rem <= 10)) {
        g_tones.playAlert(rem);
        g_prepBeepS = rem;
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
    return rem;
}

// The picker is up, but prep is what is really running underneath it. Everything
// that drives prep — the COUNT re-sync, START, the local clock — has to keep
// treating this as prep or the round opens with a frozen screen behind a picker.
static bool _pickingFromPrep() {
    return g_state == STATE_TARGET_SET && g_pickReturnState == STATE_PREP;
}

static void _openTargetPicker(AppState returnTo) {
    g_pickReturnState = returnTo;
    g_pickNone        = true;      // start on "---" so a stray press changes nothing
    g_pickWindow      = false;
    g_pickMin         = 0;
    g_pickSec         = 0;
    g_state           = STATE_TARGET_SET;
}

static void _confirmTargetPicker() {
    if (g_pickNone) {
        // "---" is the escape: there is no L-hold to spare for a cancel.
        Serial.println("[MAIN] Target picker cancelled");
    } else if (g_pickWindow) {
        // W resolves to a real number of seconds NOW, so a concrete time reaches
        // the timekeeper, the scoring and the GliderScore export. The rulebook
        // has the helper write the letter; the score is still a time.
        //
        // ⚠ In prep the working clock has not started, so getRemaining() is not
        // the answer — it would read zero and refuse a perfectly legal call. The
        // rest of a window that has not opened is the whole window.
        const int rem = (g_pickReturnState == STATE_PREP) ? g_wtMinutes * 60
                                                          : g_wt.getRemaining();
        if (g_windowUsed) {
            Serial.println("[MAIN] W refused — already used this round, one attempt only");
        } else if (rem <= 0) {
            Serial.println("[MAIN] W refused — no working time left to call");
        } else {
            // Armed, deliberately NOT resolved — see g_targetWindowPending. If the
            // glider is already in the air, the launch has happened and there is
            // nothing left to wait for, so resolve against the clock right now.
            g_windowUsed          = true;
            g_targetWindowPending = true;
            if (g_ft.isRunning()) {
                _resolveWindowTarget();
            } else {
                Serial.printf("[MAIN] Target declared: W (%ds now; resolves at launch)\n", rem);
            }
        }
    } else if (g_pickMin == 0 && g_pickSec == 0) {
        // 0:00 is indistinguishable from TARGET_NONE_S once stored, so confirming
        // it would look like a declared call and behave like no call at all. It is
        // also not a legal target. Treated as the escape, and said out loud.
        Serial.println("[MAIN] Target picker cancelled — 0:00 is not a call");
    } else {
        g_targetS      = g_pickMin * 60 + g_pickSec;
        g_targetWindow = false;
        Serial.printf("[MAIN] Target declared: %d:%02d\n", g_pickMin, g_pickSec);
    }
    // Remember that this call was made before the window opened, so _startRound()
    // knows not to clear it when START lands a moment later.
    g_prepDeclared = (g_pickReturnState == STATE_PREP &&
                      (g_targetS > TARGET_NONE_S || g_targetWindowPending));
    g_state = g_pickReturnState;
}

// Judge a target flight the instant it lands, and re-arm or advance. There is
// deliberately NO confirmation screen: the timer knows both numbers so the answer
// is not in doubt, and a screen to dismiss is exactly wrong in a turn-around.
// The result is flashed on the working-time screen instead. [TF-10]/[TF-11]
//
// Judged strictly — achieved iff flown >= target, as FAI says ("reached or
// exceeded"). No tolerance: the picker reaches every second, so any legal call
// can be entered exactly, and a tolerance would only ever inflate the score.
static void _judgeTarget(unsigned long durMs) {
    if (g_targetS <= TARGET_NONE_S) return;

    // ⚠ A W is achieved by still flying when the window shuts, not by arithmetic.
    // Its resolved target is the time left at the launch, so a flight ended by
    // expiry lands within milliseconds of it and truncating seconds can put it one
    // under — failing the only call it is possible to fly perfectly.
    const bool hit = (g_targetWindow && g_flightEndedByExpiry) ||
                     ((int)(durMs / 1000UL) >= g_targetS);
    g_targetMsgHit     = hit;
    g_targetMsgUntilMs = millis() + 3000;
    Serial.printf("[MAIN] Target %ds%s vs flight %.1fs -> %s\n",
                  g_targetS, g_targetWindow ? " (W)" : "",
                  durMs / 1000.0f, hit ? "ACHIEVED" : "MISSED");

    if (!hit) {
        // FAI: a missed call "cannot be changed" — the same target stays armed and
        // is re-flown. ⚠ Except a W, which gets ONLY ONE attempt.
        if (g_targetWindow) {
            g_targetS      = TARGET_NONE_S;
            g_targetWindow = false;
            Serial.println("[MAIN] W missed — no re-fly, one attempt only");
        }
        return;
    }

    g_targetsScored++;
    if (g_comms.getTargetMode() == TARGET_LADDER) {
        // Advance only on success, so a miss repeats the rung. That is what makes
        // it a ladder rather than a sequence, and it is true of all three: D climbs
        // by a step forever, K and M walk an explicit list and end when it runs out.
        if (g_comms.getLadderRungCount() > 0) {
            const int next = g_comms.getLadderRungS(g_targetsScored);
            g_targetS = (next > 0) ? next : TARGET_NONE_S;   // 0 = ladder finished
            if (g_targetS == TARGET_NONE_S)
                Serial.println("[MAIN] Ladder complete — all rungs reached");
        } else {
            g_targetS = g_targetS + g_comms.getLadderStepS();
        }
        g_targetWindow = false;
    } else {
        // Poker: the next call is the pilot's to make.
        g_targetS      = TARGET_NONE_S;
        g_targetWindow = false;
    }
}

static void _recordFlight() {
    unsigned long dur = g_ft.stop();
    g_log.addFlight(dur);

    // ⚠ Capture the target BEFORE judging it. _judgeTarget() clears g_targetS on
    // a hit (Poker) or advances it (Ladder), so reading it afterwards would
    // report target=0 for precisely the flights that achieved their call — the
    // ones the score is made of.
    const int  flightTargetS = g_targetS;
    const bool flightWindow  = g_targetWindow;

    if (!g_jumpedStart) _judgeTarget(dur);   // a jumped start never had a target
    if (g_jumpedStart) {
        // Deliberately no sendScratch() here, unlike the manual scratch: a jumped
        // start is never sent as a FLIGHT in the first place (JUMPED goes instead,
        // and is a CD note only), so there is no row at the base to scratch.
        g_log.scratchLast();
        g_jumpedStart = false;
        g_comms.sendJumped(g_selectedPilotId, dur);  // CD note only — never scored
        Serial.printf("[MAIN] Jumped start — flight %.2fs invalidated\n", dur / 1000.0f);
    } else {
        g_history.recordFlight(dur);
        g_comms.sendFlight(g_selectedPilotId, dur, flightTargetS, flightWindow);
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
        Serial.printf("[MAIN] Task update from base: %d min, %s, task=%s mode=%d\n",
                      g_wtMinutes, g_isF5K ? "F5K" : "F3K",
                      g_comms.getTaskCode(), (int)g_comms.getTargetMode());
    }

    // Base is the master clock. Only meaningful while working time is actually
    // running — outside that there is no clock to steer, and applying it would
    // put a stale number on an idle screen. [I-51]
    if (g_comms.hasWtSync()) {
        const int secs = g_comms.getWtSyncSeconds();
        if (g_state == STATE_WORKING_TIME_RUNNING || g_state == STATE_FLIGHT_RUNNING) {
            g_wt.syncRemaining(secs);
            Serial.printf("[MAIN] Working time synced to %ds by base\n", secs);
        } else {
            Serial.printf("[MAIN] WTSYNC %ds ignored — state=%d, no working clock\n",
                          secs, (int)g_state);
        }
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
        if (g_state == STATE_PREP || _pickingFromPrep()) {
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
         g_state == STATE_COUNTDOWN || g_state == STATE_PREP ||
         _pickingFromPrep())) {
        // ⚠ _pickingFromPrep() must be here: without it a caller still holding the
        // picker open when the window opened would never start the round at all.
        // An unconfirmed call is abandoned — the window is open, that outranks it.
        if (g_state != STATE_IDLE && g_state != STATE_PILOT_SELECT) g_tones.playWindowOpen();
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
    const bool btnR_longClick = g_btns.btnBLongClicked(); // R 800-2000ms, on release
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
            const int rem = _tickPrepClock();
            if (g_state != STATE_PREP) break;   // the fallback start fired
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
            // L declares the first target during prep. Prep is when the caller
            // actually has time to think and write the call down; once the window
            // opens they are working. R stays locked until PREP_UNLOCK_S either
            // way, so this cannot become an accidental launch. [TF-10]
            if (btnL && _pokerCanDeclare()) {
                _openTargetPicker(STATE_PREP);
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
                g_flightEndedByExpiry = false;
                _resolveWindowTarget();   // a called W becomes a number here
                g_state = STATE_FLIGHT_RUNNING;
            } else if (btnL) {
                // In Poker, L declares the next target instead of scratching.
                // Kris: "no need for scratch for Poker as you do not move on from
                // the time the pilot sets until they achieve it" — a bad flight
                // simply gets re-flown against the same call, so there is nothing
                // to discard. Ladder keeps scratch: its target is not the pilot's
                // to set, so L has nothing else to do. [TF-10]
                if (_pokerCanDeclare()) {
                    _openTargetPicker(STATE_WORKING_TIME_RUNNING);
                } else if (g_log.count() > 0) {
                    // L click: scratch last flight (if any recorded)
                    g_scratchStartMs = millis();
                    g_state = STATE_SCRATCH_CONFIRM;
                }
                // L click with no flights: do nothing (can't scratch what doesn't exist)
            }
            break;

        case STATE_FLIGHT_RUNNING:
            if (g_wt.isExpired()) {
                // Still airborne at the close of the window — that is precisely
                // what a W call is, so record the fact before judging it.
                g_flightEndedByExpiry = true;
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
            } else if (btnL && _pokerCanDeclare()) {
                // Declaring AFTER the launch is explicitly permitted — FAI 2025
                // F3K.11.5: "shown to the timekeeper in written numbers (e.g.
                // 2:38) ... immediately after the launch". This is the quick
                // turn-around: throw first, call the target while it climbs.
                _openTargetPicker(STATE_FLIGHT_RUNNING);
            }
            break;

        case STATE_TARGET_SET: {
            // Prep keeps counting behind the picker — see _tickPrepClock().
            if (_pickingFromPrep()) {
                _tickPrepClock();
                if (g_state != STATE_TARGET_SET) break;   // window opened under us
            }
            const int maxMin = g_wtMinutes > 0 ? g_wtMinutes : 10;
            if (btnL) {
                // Cycle: --- -> W -> 0 -> 1 -> ... -> max -> ---
                // W and "---" live in this wrap because there is no L-hold to
                // spare for either: holding L is an AXP2101 power-off that
                // software never sees.
                //
                // ⚠ W is FIRST, one press from opening. Kris: every other call is
                // a fixed number and loses nothing while you dial it, but W is
                // "the rest of the window" and is getting shorter the whole time
                // you spend selecting it — so it is the one call that must be
                // fast. It is also the natural quick-turn-around call.
                if (g_pickNone)                    { g_pickNone = false; g_pickWindow = true; }
                else if (g_pickWindow)             { g_pickWindow = false; g_pickMin = 0; g_pickSec = 0; }
                else if (g_pickMin >= maxMin)      { g_pickNone = true; }
                else                               { g_pickMin++; }
            } else if (btnR_veryLong) {
                // Confirm. This is the 2s hold, NOT the 800ms one, because the
                // 800ms hold fires while the button is still down and so ended the
                // screen before any longer gesture could be made — which left the
                // fine adjust below permanently unreachable. Field-found.
                _confirmTargetPicker();
            } else if (btnR_longClick) {
                // Fine adjust, +1s. The rulebook's own example call is 2:38, so a
                // 5-second-only picker could not express a legal target — and
                // judging strictly against a number the timekeeper cannot enter
                // would punish the pilot for our UI.
                if (!g_pickWindow) {
                    g_pickNone = false;    // same escape from "---" as a plain click
                    g_pickSec  = (g_pickSec + TARGET_PICK_FINE_S) % 60;
                }
            } else if (btnR) {
                // ⚠ R must work on "---" or a sub-minute call is unreachable in
                // practice: the picker opens on "---", and requiring an L press
                // first meant three R clicks did nothing at all. Field-found —
                // the timekeeper read it as "you cannot set under a minute".
                if (!g_pickWindow) {
                    g_pickNone = false;
                    g_pickSec  = (g_pickSec + TARGET_PICK_SEC_STEP) % 60;
                }
            }
            break;
        }

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
                // R click: confirm scratch. The flight was reported to the base
                // the moment it was flown, so scratching it locally is only half
                // the job — without the SCRATCH the base keeps scoring it and it
                // reaches the GliderScore export as a valid time. [I-42]
                unsigned long scratched = g_log.scratchLast();
                if (scratched > 0) {
                    g_comms.sendScratch(g_selectedPilotId, scratched);
                    Serial.printf("[MAIN] Scratched flight %.2fs — SCRATCH sent\n",
                                  scratched / 1000.0f);
                }
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
            // Clears a NO WIFI left by arriving here before the radio associated.
            g_ota.retryIfWifiReturned();
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
                } else if (btnL) {
                    // Manual re-check. The auto-retry above only covers NO_WIFI;
                    // a FAILED (base station unreachable, or serving something
                    // unparseable) needs a way back that is not four settings
                    // holds. Harmless on the other statuses.
                    g_ota.check();
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
    //
    // OTA_AVAILABLE counts too, and for a different reason: the screen is asking
    // a question and waiting for an answer. It used to blank after the 2 min
    // inactivity period with the offer still on it, which reads as a hang right
    // at the moment the user is deciding whether to trust a firmware update.
    // The terminal states (UP_TO_DATE, FAILED, NO_WIFI, IDLE) are deliberately
    // left blankable — nothing is pending, and this screen has no auto-exit, so
    // a timer parked here overnight would otherwise ghost the panel.
    if (g_state == STATE_OTA_CHECK &&
        (g_ota.getStatus() == OTA_CHECKING   ||
         g_ota.getStatus() == OTA_DOWNLOADING ||
         g_ota.getStatus() == OTA_AVAILABLE)) {
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
        // Hold the audio amp up for the whole round so only the first beep pays
        // the 50 ms power-on settle, instead of every one of them. [I-35]
        g_tones.holdAmp(_roundLive(g_state));
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
