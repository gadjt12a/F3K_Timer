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
    void   scratchLast();                        // marks most-recent valid flight scratched

    int    count()    const { return _count; }
    bool   isFull()   const { return _count >= MAX_FLIGHTS; }
    Flight get(int i) const;
    int    bestIndex() const;  // shortest valid flight; -1 if none

private:
    Flight _flights[MAX_FLIGHTS];
    int    _count = 0;
};
