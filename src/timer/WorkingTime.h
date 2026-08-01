#pragma once
#include <Arduino.h>
#include "config.h"

class WorkingTime {
public:
    using AlertCb = void (*)(int timeRemaining, void* ctx);

    void begin(int totalSeconds = DEFAULT_WORKING_TIME);
    void start();
    void reset();
    void update();

    // Move the running clock to `seconds` remaining, without disturbing anything
    // else — not the total, not the running/started flags, not the flight log.
    //
    // The base is the master clock and there was no way to say so. `TASK wt=` is
    // whole MINUTES and is only read when a round starts, and `START` is ignored
    // by an already-running timer — so a mid-round reconnect that arrived with
    // 45 s left was told zero, and one with 8:30 left was told 8:00. [I-51]
    void syncRemaining(int seconds);

    int           getRemaining()   const { return (int)(_remainingMs / 1000); }
    unsigned long getRemainingMs() const { return _remainingMs; }
    int           getTotal()       const { return _total; }
    unsigned long getTotalMs()     const { return (unsigned long)_total * 1000UL; }
    bool          isRunning()      const { return _running; }
    bool          isExpired()      const { return _started && _remainingMs == 0; }

    void setAlertCallback(AlertCb cb, void* ctx = nullptr) {
        _cb = cb;  _cbCtx = ctx;
    }

private:
    int           _total        = DEFAULT_WORKING_TIME;
    unsigned long _remainingMs  = DEFAULT_WORKING_TIME * 1000UL;
    bool          _running      = false;
    bool          _started      = false;
    unsigned long _lastUpdateMs = 0;
    int           _lastAlertSec = -1;
    bool          _fired[ALERT_COUNT] = {};
    AlertCb       _cb    = nullptr;
    void*         _cbCtx = nullptr;
};
