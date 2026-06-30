#include "FlightTimer.h"

void FlightTimer::start() {
    if (_running) return;
    _startMs   = millis();
    _stoppedMs = 0;
    _running   = true;
}

unsigned long FlightTimer::stop() {
    if (!_running) return _stoppedMs;
    _stoppedMs = millis() - _startMs;
    _running   = false;
    return _stoppedMs;
}

unsigned long FlightTimer::elapsed() const {
    if (!_running) return _stoppedMs;
    return millis() - _startMs;
}

void FlightTimer::reset() {
    _running   = false;
    _startMs   = 0;
    _stoppedMs = 0;
}
