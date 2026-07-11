#pragma once
#include <Arduino.h>
#include "config.h"

#ifdef WAVESHARE_HW
#include <XPowersLib.h>
#endif

class Buttons {
public:
    void begin();
    void update();

    bool btnAClicked()   const;
    bool btnBClicked()   const;
    bool btnAHeld()      const;   // 800ms hold
    bool btnBHeld()      const;   // 800ms hold
    bool btnAVeryLong()  const;   // 2000ms hold (for abort)
    bool btnBVeryLong()  const;   // 2000ms hold (for abort)

    // Battery info (hardware only — returns defaults on Wokwi)
    int  getBatteryPercent();  // 0-100
    bool isCharging();

private:
#ifdef WAVESHARE_HW
    XPowersAXP2101 _pmu;
#endif

    bool _prevA = false, _prevB = false;
    unsigned long _pressedAms = 0, _pressedBms = 0;
    unsigned long _lastBChangeMs = 0;  // debounce tracking
    unsigned long _lastAClickMs  = 0;  // cooldown: suppress duplicate PKEY IRQs per press
    bool _holdFiredA = false, _holdFiredB = false;
    bool _veryLongFiredA = false, _veryLongFiredB = false;
    bool _clickA = false, _clickB = false;
    bool _holdA  = false, _holdB  = false;
    bool _veryLongA = false, _veryLongB = false;

    static const unsigned long DEBOUNCE_MS = 50;
    static const unsigned long VERY_LONG_PRESS_MS = 2000;
};
