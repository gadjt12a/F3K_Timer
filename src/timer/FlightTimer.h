#pragma once
#include <Arduino.h>

class FlightTimer {
public:
    void          start();
    unsigned long stop();           // stops and returns elapsed ms
    unsigned long elapsed() const;  // ms while running, or stopped value
    bool          isRunning() const { return _running; }
    void          reset();

private:
    unsigned long _startMs  = 0;
    unsigned long _stoppedMs = 0;
    bool          _running  = false;
};
