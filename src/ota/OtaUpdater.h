#pragma once
#ifdef WAVESHARE_HW

#include <Arduino.h>
#include "config.h"

class OtaUpdater {
public:
    void begin();
    void check();        // async version check (FreeRTOS task)
    void startUpdate();  // async firmware download + flash (FreeRTOS task)

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
