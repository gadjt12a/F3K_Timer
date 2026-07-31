#pragma once
#include <Arduino.h>
#include "config.h"

struct Flight {
    unsigned long durationMs;
    bool          scratched;
};

class FlightLog {
public:
    void   reset();
    bool   addFlight(unsigned long durationMs);  // false if full
    // Marks the most-recent valid flight scratched. Returns its duration so the
    // caller can tell the base station which flight went — the base identifies a
    // flight by (pilot, group, duration), the same key its dedup uses. 0 if there
    // was nothing left to scratch.
    unsigned long scratchLast();

    int    count()    const { return _count; }
    bool   isFull()   const { return _count >= MAX_FLIGHTS; }
    Flight get(int i) const;
    int    bestIndex() const;  // shortest valid flight; -1 if none

private:
    Flight _flights[MAX_FLIGHTS];
    int    _count = 0;
};
