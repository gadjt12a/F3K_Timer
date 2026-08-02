#pragma once
#ifdef WAVESHARE_HW

#include <Arduino.h>
#include "config.h"

class OtaUpdater {
public:
    void begin();
    void check();        // async version check (FreeRTOS task)
    void startUpdate();  // async firmware download + flash (FreeRTOS task)

    // Flash whatever the base station is serving, newer or older, without asking.
    // Used only for a base-station PUSH: firmware is base-managed, and a CD who has
    // confirmed a downgrade has overruled the timer's own judgement on purpose.
    // ⚠ This is the ONLY way past the OTA_BASE_OLDER refusal that [I-41] added.
    // Nothing on the timer's own buttons may call it.
    void forceUpdate();

    // Re-run a check that failed only because the radio was not up yet. Safe to
    // call every loop: it does nothing unless the status is OTA_NO_WIFI and WiFi
    // has since associated. Keeps the WiFi dependency in here rather than making
    // main.cpp include WiFi.h to ask the same question.
    void retryIfWifiReturned();

    OtaStatus   getStatus()           const { return (OtaStatus)_status; }
    int         getProgress()         const { return _progress; }
    const char* getAvailableVersion() const { return _availVer; }

private:
    static void _checkTask(void* pv);
    static void _updateTask(void* pv);

    volatile uint8_t _status   = (uint8_t)OTA_IDLE;
    volatile int     _progress = 0;
    char _availVer[32] = "";
};

#endif // WAVESHARE_HW
