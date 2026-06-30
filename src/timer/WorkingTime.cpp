#include "WorkingTime.h"
#include "config.h"

void WorkingTime::begin(int totalSeconds) {
    _total      = totalSeconds;
    _remaining  = totalSeconds;
    _running    = false;
    _started    = false;
    _lastTickMs = 0;
    for (int i = 0; i < ALERT_COUNT; i++) _fired[i] = false;
}

void WorkingTime::start() {
    if (_running) return;
    _running    = true;
    _started    = true;
    _lastTickMs = millis();
}

void WorkingTime::reset() {
    _running    = false;
    _started    = false;
    _remaining  = _total;
    _lastTickMs = 0;
    for (int i = 0; i < ALERT_COUNT; i++) _fired[i] = false;
}

void WorkingTime::update() {
    if (!_running) return;

    unsigned long now = millis();
    if (now - _lastTickMs < 1000) return;

    int ticks   = (int)((now - _lastTickMs) / 1000);
    _lastTickMs += (unsigned long)ticks * 1000;

    int prev   = _remaining;
    _remaining -= ticks;
    if (_remaining < 0) _remaining = 0;

    // Fire alert for every threshold crossed from above in this tick
    for (int i = 0; i < ALERT_COUNT; i++) {
        if (!_fired[i] && prev > ALERT_TIMES[i] && _remaining <= ALERT_TIMES[i]) {
            _fired[i] = true;
            if (_cb) _cb(ALERT_TIMES[i], _cbCtx);
        }
    }

    if (_remaining <= 0) _running = false;
}
