#include "FlightLog.h"

void FlightLog::reset() {
    _count = 0;
}

bool FlightLog::addFlight(unsigned long durationMs) {
    if (_count >= MAX_FLIGHTS) return false;
    _flights[_count] = {durationMs, false, 0, false};
    _count++;
    return true;
}

void FlightLog::markTargetLast(int targetS, bool achieved) {
    if (_count <= 0) return;
    _flights[_count - 1].targetS  = targetS;
    _flights[_count - 1].achieved = achieved;
}

bool FlightLog::hasTargets() const {
    for (int i = 0; i < _count; i++)
        if (_flights[i].targetS > 0) return true;
    return false;
}

unsigned long FlightLog::scratchLast() {
    for (int i = _count - 1; i >= 0; i--) {
        if (!_flights[i].scratched) {
            _flights[i].scratched = true;
            return _flights[i].durationMs;
        }
    }
    return 0;
}

Flight FlightLog::get(int i) const {
    if (i < 0 || i >= _count) return {0, false, 0, false};
    return _flights[i];
}

int FlightLog::bestIndex() const {
    int best = -1;
    for (int i = 0; i < _count; i++) {
        if (_flights[i].scratched || _flights[i].durationMs == 0) continue;
        if (best < 0 || _flights[i].durationMs > _flights[best].durationMs)
            best = i;
    }
    return best;
}
