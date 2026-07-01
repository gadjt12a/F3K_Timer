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

    int currentSec = (int)(_remainingMs / 1000);

    // Fire alerts when crossing second thresholds
    if (_lastAlertSec != currentSec) {
        for (int i = 0; i < ALERT_COUNT; i++) {
            if (!_fired[i] && _lastAlertSec > ALERT_TIMES[i] && currentSec <= ALERT_TIMES[i]) {
                _fired[i] = true;
                if (_cb) _cb(ALERT_TIMES[i], _cbCtx);
            }
        }
        _lastAlertSec = currentSec;
    }

    if (_remainingMs == 0) _running = false;
}
