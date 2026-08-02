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
    // Fires on RELEASE when the press lasted 800-2000ms — i.e. "held, but let go
    // before the very-long threshold". Everything else fires while the button is
    // still down, which is why a 2s gesture could never be offered alongside an
    // 800ms one: the 800ms action ran first and the state was already gone. The
    // Poker picker needs three distinct R gestures (+5s, +1s, confirm), so this
    // is the middle one. Ignore it in any state that acts on btnBHeld().
    bool btnBLongClicked() const;

    // Battery info (hardware only — returns defaults on Wokwi)
    int  getBatteryPercent();  // 0-100
    bool isCharging();
    // VBUS present, i.e. a USB cable is plugged in. Used to tell "on a desk or
    // wired to the base station" from "in a caller's hand at a competition".
    bool isUsbPowered();

private:
#ifdef WAVESHARE_HW
    XPowersAXP2101 _pmu;
#endif

    bool _pmuOk = false;   // AXP2101 came up; without it VBUS is unreadable
    bool _prevA = false, _prevB = false;
    unsigned long _pressedAms = 0, _pressedBms = 0;
    unsigned long _lastBChangeMs = 0;  // debounce tracking
    unsigned long _lastAClickMs         = 0;  // cooldown: suppress bouncy POSITIVE edges
    unsigned long _startupIgnoreUntilMs = 0;  // suppress A clicks until this millis()
    bool          _firstUpdateDone      = false; // tracks whether first update() ran yet
    bool _holdFiredA = false, _holdFiredB = false;
    bool _veryLongFiredA = false, _veryLongFiredB = false;
    bool _clickA = false, _clickB = false;
    bool _longClickB = false;
    bool _holdA  = false, _holdB  = false;
    bool _veryLongA = false, _veryLongB = false;

    static const unsigned long DEBOUNCE_MS = 50;
    static const unsigned long VERY_LONG_PRESS_MS = 2000;
};
