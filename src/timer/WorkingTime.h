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

    int  getRemaining() const { return _remaining; }
    int  getTotal()     const { return _total; }
    bool isRunning()    const { return _running; }
    bool isExpired()    const { return _started && _remaining <= 0; }

    void setAlertCallback(AlertCb cb, void* ctx = nullptr) {
        _cb = cb;  _cbCtx = ctx;
    }

private:
    int           _total      = DEFAULT_WORKING_TIME;
    int           _remaining  = DEFAULT_WORKING_TIME;
    bool          _running    = false;
    bool          _started    = false;
    unsigned long _lastTickMs = 0;
    bool          _fired[ALERT_COUNT] = {};
    AlertCb       _cb    = nullptr;
    void*         _cbCtx = nullptr;
};
