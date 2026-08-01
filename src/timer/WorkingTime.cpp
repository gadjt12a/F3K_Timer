#include "WorkingTime.h"
#include "config.h"

void WorkingTime::begin(int totalSeconds) {
    _total        = totalSeconds;
    _remainingMs  = (unsigned long)totalSeconds * 1000UL;
    _running      = false;
    _started      = false;
    _lastUpdateMs = 0;
    _lastAlertSec = -1;
    for (int i = 0; i < ALERT_COUNT; i++) _fired[i] = false;
}

void WorkingTime::start() {
    if (_running) return;
    _running      = true;
    _started      = true;
    _lastUpdateMs = millis();
}

void WorkingTime::reset() {
    _running      = false;
    _started      = false;
    _remainingMs  = (unsigned long)_total * 1000UL;
    _lastUpdateMs = 0;
    _lastAlertSec = -1;
    for (int i = 0; i < ALERT_COUNT; i++) _fired[i] = false;
}

void WorkingTime::syncRemaining(int seconds) {
    if (seconds < 0) seconds = 0;
    _remainingMs = (unsigned long)seconds * 1000UL;

    // Without this the next update() subtracts everything since the last tick and
    // undoes the jump on the spot.
    _lastUpdateMs = millis();

    // Same ceiling as update() uses, so the alert bookkeeping below lines up with
    // the comparison there. [I-33]
    int alertSec  = (int)((_remainingMs + 999UL) / 1000UL);
    _lastAlertSec = alertSec;

    // Re-base which alerts have "already happened". A jump from 10:00 to 0:15
    // leaves the 30 s and 20 s calls unfired, and without this they would all
    // fire the instant the clock crossed them again — three announcements on top
    // of each other. Thresholds still ahead of us are re-armed for the same
    // reason in reverse, so a sync in either direction leaves a consistent state.
    for (int i = 0; i < ALERT_COUNT; i++) {
        _fired[i] = (ALERT_TIMES[i] >= alertSec);
    }

    if (_remainingMs == 0) _running = false;
}

void WorkingTime::update() {
    if (!_running) return;

    unsigned long now = millis();
    unsigned long elapsed = now - _lastUpdateMs;
    _lastUpdateMs = now;

    if (elapsed >= _remainingMs) {
        _remainingMs = 0;
    } else {
        _remainingMs -= elapsed;
    }

    // CEILING, not truncation. _remainingMs / 1000 rounds down, so "30" first
    // appears with 30.999 s left and every alert fired a full second early — the
    // caller's timer counted "3, 2, 1" while a whole second of working time
    // remained, and the base station's horn arrived after the timer had already
    // said zero. Measured against the base's own cue log: -985, -993, -986 ms.
    //
    // Deliberately NOT getRemaining(): that drives the display and the arc, and
    // changing what is on screen is not part of fixing what is heard. [I-33]
    int alertSec = (int)((_remainingMs + 999UL) / 1000UL);

    // Fire alerts when crossing second thresholds
    if (_lastAlertSec != alertSec) {
        for (int i = 0; i < ALERT_COUNT; i++) {
            if (!_fired[i] && _lastAlertSec > ALERT_TIMES[i] && alertSec <= ALERT_TIMES[i]) {
                _fired[i] = true;
                if (_cb) _cb(ALERT_TIMES[i], _cbCtx);
            }
        }
        _lastAlertSec = alertSec;
    }

    if (_remainingMs == 0) _running = false;
}
