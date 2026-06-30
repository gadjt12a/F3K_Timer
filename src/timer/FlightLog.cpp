#include "FlightLog.h"

void FlightLog::reset() {
    _count = 0;
}

bool FlightLog::addFlight(unsigned long durationMs) {
    if (_count >= MAX_FLIGHTS) return false;
    _flights[_count] = {durationMs, false};
    _count++;
    return true;
}

void FlightLog::scratchLast() {
    for (int i = _count - 1; i >= 0; i--) {
        if (!_flights[i].scratched) {
            _flights[i].scratched = true;
            return;
        }
    }
}

Flight FlightLog::get(int i) const {
    if (i < 0 || i >= _count) return {0, false};
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
