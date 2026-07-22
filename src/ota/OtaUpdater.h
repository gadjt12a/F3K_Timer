#pragma once
#ifdef WAVESHARE_HW

#include <Arduino.h>
#include "config.h"

class OtaUpdater {
public:
    void begin();
    void check();        // async version check (FreeRTOS task)
    void startUpdate();  // async firmware download + flash (FreeRTOS task)

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
