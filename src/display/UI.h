#pragma once

#include "config.h"
#include "timer/WorkingTime.h"
#include "timer/FlightTimer.h"
#include "timer/FlightLog.h"

#ifdef WOKWI_SIM
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#define TFT_CS  5
#define TFT_DC  2
#define TFT_RST 4
#else
#include <Arduino_GFX_Library.h>
#endif

class UI {
public:
    void begin();

    void render(AppState state,
                const WorkingTime& wt,
                const FlightTimer& ft,
                const FlightLog&   log,
                unsigned long      scratchStartMs = 0,
                int                wtMinutes      = 10,
                int                batteryPct     = -1,
                bool               isCharging     = false);

private:
#ifdef WOKWI_SIM
    Adafruit_ILI9341 _tft{TFT_CS, TFT_DC, TFT_RST};
#else
    Arduino_DataBus*  _bus     = nullptr;
    Arduino_GFX*      _display = nullptr;  // Raw CO5300 display (no rotation)
    Arduino_Canvas*   _gfx     = nullptr;  // Canvas with software rotation
#endif

    AppState _prevState    = (AppState)255;
    int      _prevWtSecs   = -1;
    int      _prevFlashSecs = -1;
    bool     _arcVisible   = true;
    unsigned long _lastArcSweepMs = 0;  // For sub-second arc sweep animation
    int      _prevBatteryPct = -1;      // Track battery changes

    void _drawRunningFull(bool flightActive,
                          const WorkingTime& wt,
                          const FlightTimer& ft,
                          const FlightLog&   log);
    void _updateRunningInc(bool flightActive,
                           const WorkingTime& wt,
                           const FlightTimer& ft);
    void _updateFlightStateOnly(bool flightActive, const FlightTimer& ft, const FlightLog& log);

    void _drawIdle();
    void _drawExpired(const FlightLog& log);

    void _drawSettings(int minutes);
    void _drawSettingsInc(int minutes);

    void _drawFlightLog(const FlightLog& log,
                        int startY   = 0,
                        int maxShown = 3);
    void _drawFlightLogExpired(const FlightLog& log,
                               int startY,
                               int maxShown);
    void _drawCentered(const char* str, int cx, int cy, uint16_t color, uint8_t size);
#ifndef WOKWI_SIM
    void _drawFontCentered(const char* str, int cx, int cy, uint16_t color, const GFXfont* font);
#endif
    void _updateArc(int remaining, int total);
    void _drawArc(int remaining, int total, uint16_t color);
    void _drawArcSegment(float startDeg, float endDeg, uint16_t color);
    void _drawMultiColorArc(int remaining, int total);
#ifndef WOKWI_SIM
    void _eraseArcSlice(float startDeg, float endDeg);
#endif
    void _clearScreen();
    void _drawBattery(int pct, bool charging);

    static char* fmtSec(int seconds, char* buf, size_t len);
    static char* fmtMs(unsigned long ms, char* buf, size_t len);
};
