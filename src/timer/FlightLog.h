#pragma once
#include <Arduino.h>
#include "config.h"

struct Flight {
    unsigned long durationMs;
    bool          scratched;
    // What this flight was flown against, on a target task (Poker or a Ladder).
    // targetS == 0 means there was no target, which is also how the results screen
    // tells an ordinary task from a target one without being told which it is.
    int           targetS;
    bool          achieved;
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

    // Record how the most-recent flight was judged against its target. Separate
    // from addFlight() because the judging happens after the flight is logged.
    void   markTargetLast(int targetS, bool achieved);
    // Any flight flown against a target — i.e. this was a Poker or Ladder round.
    bool   hasTargets() const;

    int    count()    const { return _count; }
    bool   isFull()   const { return _count >= MAX_FLIGHTS; }
    Flight get(int i) const;
    int    bestIndex() const;  // shortest valid flight; -1 if none

private:
    Flight _flights[MAX_FLIGHTS];
    int    _count = 0;
};
