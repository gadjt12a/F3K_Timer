#include "UI.h"
#include <stdio.h>
#include <math.h>

#ifdef WOKWI_SIM
#include <SPI.h>
#endif

#include "pin_config.h"

// FreeFonts for better typography
#include "fonts/FreeSansBold24pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSans12pt7b.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeMonoBold24pt7b.h"
#include "fonts/FreeMonoBold18pt7b.h"

// ── Colour definitions (RGB565) ──────────────────────────────────────────────
#define C565(r,g,b) (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

static const uint16_t COL_BG      = 0x0000;
static const uint16_t COL_WHITE   = 0xFFFF;
static const uint16_t COL_GRAY    = C565(0x88, 0x88, 0x88);
static const uint16_t COL_DIMGRAY = C565(0x44, 0x44, 0x44);
static const uint16_t COL_RED     = C565(0xFF, 0x20, 0x20);
static const uint16_t COL_ORANGE  = C565(0xFF, 0x88, 0x00);
static const uint16_t COL_YELLOW  = C565(0xFF, 0xDD, 0x00);
static const uint16_t COL_GREEN   = C565(0x00, 0xC8, 0x00);

static const uint16_t COL_FUCHSIA = C565(0xFF, 0x40, 0xFF);

static const uint16_t COL_ARC_GREEN  = C565(0x00, 0xC8, 0x00);
static const uint16_t COL_ARC_ORANGE = C565(0xFF, 0x88, 0x00);
static const uint16_t COL_ARC_RED    = C565(0xFF, 0x20, 0x20);

// ── Layout constants ─────────────────────────────────────────────────────────
#ifdef WOKWI_SIM
// Wokwi ILI9341 240x320 rectangular layout
static const int Y_WT_LABEL  =  80;
static const int Y_WT_DIGITS = 112;
static const int Y_DIV1      = 136;
static const int Y_FL_LABEL  = 152;
static const int Y_FL_DIGITS = 174;
static const int Y_DIV2      = 197;
static const int Y_LOG_START = 214;
static const int Y_LOG_STEP  =  17;
static const int DIV_HALF    =  80;
#else
// Waveshare 466x466 round AMOLED radial layout
// Center at 233, 233
static const int WS_CX = 233;
static const int WS_CY = 233;
static const int WS_R_SAFE = 210;

// Running screen layout (top to bottom):
// Flight time (large) at top - this is what caller watches most
static const int WS_Y_FL_LABEL  = 90;
static const int WS_Y_FL_DIGITS = 140;   // large flight time
// State label (FLY/WAIT)
static const int WS_Y_STATE     = 200;
// Flight log (3 best times) in middle
static const int WS_Y_LOG_START = 250;
static const int WS_Y_LOG_STEP  = 32;
// Working time at bottom (less prominent)
static const int WS_Y_WT_DIGITS = 380;
// Idle screen
static const int WS_Y_IDLE_TITLE = 200;
static const int WS_Y_IDLE_SUB   = 250;
static const int WS_Y_IDLE_HINT1 = 310;
static const int WS_Y_IDLE_HINT2 = 340;
#endif

// ─────────────────────────────────────────────────────────────────────────────

char* UI::fmtMs(unsigned long ms, char* buf, size_t len) {
    unsigned long totalSec = ms / 1000;
    unsigned long hundredths = (ms % 1000) / 10;
    snprintf(buf, len, "%02lu:%02lu.%02lu", totalSec / 60, totalSec % 60, hundredths);
    return buf;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void UI::_clearScreen() {
#ifdef WOKWI_SIM
    _tft.fillScreen(COL_BG);
#else
    _gfx->fillScreen(COL_BG);
#endif
}

void UI::_drawCentered(const char* str, int cx, int cy, uint16_t color, uint8_t size) {
#ifdef WOKWI_SIM
    _tft.setFont(nullptr);
    _tft.setTextSize(size);
    _tft.setTextColor(color, COL_BG);
    _tft.setTextWrap(false);
    int16_t x1, y1; uint16_t w, h;
    _tft.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    _tft.setCursor(cx - (int16_t)(w / 2) - x1, cy - (int16_t)(h / 2) - y1);
    _tft.print(str);
#else
    _gfx->setFont(nullptr);
    _gfx->setTextSize(size);
    _gfx->setTextColor(color, COL_BG);
    _gfx->setTextWrap(false);
    int16_t x1, y1; uint16_t w, h;
    _gfx->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    _gfx->setCursor(cx - (int16_t)(w / 2) - x1, cy - (int16_t)(h / 2) - y1);
    _gfx->print(str);
#endif
}

// Draw text with a FreeFont (waveshare only for now)
void UI::_drawFontCentered(const char* str, int cx, int cy, uint16_t color, const GFXfont* font,
                           uint8_t size) {
#ifndef WOKWI_SIM
    _gfx->setFont(font);
    _gfx->setTextSize(size);
    _gfx->setTextColor(color, COL_BG);
    _gfx->setTextWrap(false);
    int16_t x1, y1; uint16_t w, h;
    _gfx->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    _gfx->setCursor(cx - (int16_t)(w / 2) - x1, cy - (int16_t)(h / 2) - y1);
    _gfx->print(str);
    _gfx->setFont(nullptr);  // Reset to default
#endif
}

static uint16_t _arcColor(int remaining) {
    if (remaining > ARC_GREEN_THRESHOLD)  return COL_ARC_GREEN;
    if (remaining > ARC_ORANGE_THRESHOLD) return COL_ARC_ORANGE;
    return COL_ARC_RED;
}

#ifndef WOKWI_SIM
// Custom fillCircle for CO5300 - the standard fillCircle uses writeFastHLine
// which doesn't render on this QSPI display. This version uses fillRect with 2px min height.
static void ws_fillCircle(Arduino_GFX* gfx, int cx, int cy, int r, uint16_t color) {
    for (int y = -r; y <= r; y += 2) {
        int halfWidth = (int)sqrtf((float)(r * r - y * y));
        if (halfWidth > 0) {
            gfx->fillRect(cx - halfWidth, cy + y, halfWidth * 2, 2, color);
        }
    }
}

// Custom ring drawing - draws a filled ring (donut shape)
// Uses 2px height slices because 1px doesn't render on CO5300 QSPI
static void ws_fillRing(Arduino_GFX* gfx, int cx, int cy, int outerR, int innerR, uint16_t color) {
    for (int y = -outerR; y <= outerR; y += 2) {
        int outerHalf = (int)sqrtf((float)(outerR * outerR - y * y));
        int innerHalf = (abs(y) <= innerR) ? (int)sqrtf((float)(innerR * innerR - y * y)) : 0;
        if (outerHalf > innerHalf) {
            int segWidth = outerHalf - innerHalf;
            if (segWidth > 0) {
                gfx->fillRect(cx - outerHalf, cy + y, segWidth, 2, color);
                gfx->fillRect(cx + innerHalf, cy + y, segWidth, 2, color);
            }
        }
    }
}

// Erase ring using radial approach to match _drawArcSegment's 3x3 drawing
// This ensures all pixels painted by the arc are erased
static void ws_eraseRingRadial(Arduino_GFX* gfx, int cx, int cy, int outerR, int innerR) {
    for (float angle = 0; angle <= 2.0f * M_PI + 0.01f; angle += 0.008f) {
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        for (int r = innerR - 2; r <= outerR + 2; r++) {
            int x = cx + (int)(r * cosA);
            int y = cy + (int)(r * sinA);
            gfx->fillRect(x - 1, y - 1, 3, 3, 0x0000);
        }
    }
}

#endif

// Draw arc segment from startDeg to endDeg (0=12 o'clock, CW)
void UI::_drawArcSegment(float startDeg, float endDeg, uint16_t color) {
    if (endDeg <= startDeg) return;

#ifdef WOKWI_SIM
    for (float a = startDeg; a < endDeg; a += 0.5f) {
        float rad = a * (float)M_PI / 180.0f;
        float s = sinf(rad);
        float c = cosf(rad);
        _tft.drawLine(
            DISPLAY_CX + (int)(ARC_INNER_RADIUS * s),
            DISPLAY_CY - (int)(ARC_INNER_RADIUS * c),
            DISPLAY_CX + (int)(ARC_OUTER_RADIUS * s),
            DISPLAY_CY - (int)(ARC_OUTER_RADIUS * c),
            color);
    }
#else
    float startRad = (startDeg - 90.0f) * M_PI / 180.0f;
    float endRad = (endDeg - 90.0f) * M_PI / 180.0f;
    for (float angle = startRad; angle <= endRad; angle += 0.008f) {
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        for (int r = ARC_INNER_RADIUS; r <= ARC_OUTER_RADIUS; r++) {
            int x = WS_CX + (int)(r * cosA);
            int y = WS_CY + (int)(r * sinA);
            _gfx->fillRect(x - 1, y - 1, 3, 3, color);
        }
    }
#endif
}

void UI::_drawArc(int remaining, int total, uint16_t color) {
    if (total <= 0 || remaining <= 0) return;
    float sweepDeg = (float)remaining / total * 360.0f;
    _drawArcSegment(0, sweepDeg, color);
}

// Draw multi-colored arc based on time thresholds
void UI::_drawMultiColorArc(int remaining, int total) {
    if (total <= 0 || remaining <= 0) return;

    float totalDeg = (float)remaining / total * 360.0f;
    float orangeStartDeg = (float)ARC_ORANGE_THRESHOLD / total * 360.0f;  // 30s mark
    float greenStartDeg = (float)ARC_GREEN_THRESHOLD / total * 360.0f;    // 60s mark

    if (remaining > ARC_GREEN_THRESHOLD) {
        // All green
        _drawArcSegment(0, totalDeg, COL_ARC_GREEN);
    } else if (remaining > ARC_ORANGE_THRESHOLD) {
        // All orange (between 30-60s)
        _drawArcSegment(0, totalDeg, COL_ARC_ORANGE);
    } else {
        // Under 30s: draw red from 0 to current, keep any remaining time visual
        _drawArcSegment(0, totalDeg, COL_ARC_RED);
    }
}

#ifndef WOKWI_SIM
// Erase a slice of the arc from startDeg to endDeg (0=12 o'clock, CW)
void UI::_eraseArcSlice(float startDeg, float endDeg) {
    // Convert to radians, offset by -90 so 0 deg = 12 o'clock
    float startRad = (startDeg - 90.0f) * M_PI / 180.0f;
    float endRad = (endDeg - 90.0f) * M_PI / 180.0f;

    // Use finer steps to ensure complete coverage
    // Angle step ~0.5 degrees, radius step 1 pixel
    for (float angle = startRad; angle <= endRad + 0.01f; angle += 0.008f) {
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        for (int r = ARC_INNER_RADIUS - 1; r <= ARC_OUTER_RADIUS + 1; r++) {
            int x = WS_CX + (int)(r * cosA);
            int y = WS_CY + (int)(r * sinA);
            // Use 3x3 rect to ensure pixel coverage with 2px rendering
            _gfx->fillRect(x - 1, y - 1, 3, 3, COL_BG);
        }
    }
}
#endif

void UI::_updateArc(int remaining, int total) {
    static bool wasInSweepMode = false;

    if (remaining <= ARC_RED_THRESHOLD && remaining > 0) {
        // Last 10 seconds: sweeping red arc that refills each second
        // Arc starts full at beginning of each second, sweeps CCW (erases) over 1 second
        unsigned long now = millis();

        if (!wasInSweepMode) {
            // First frame of sweep mode - clear any partial arc and draw full red ring
#ifdef WOKWI_SIM
            _drawArcSegment(0, 360, COL_BG);
            _drawArcSegment(0, 360, COL_ARC_RED);
#else
            ws_eraseRingRadial(_gfx, WS_CX, WS_CY, ARC_OUTER_RADIUS, ARC_INNER_RADIUS);
            ws_fillRing(_gfx, WS_CX, WS_CY, ARC_OUTER_RADIUS, ARC_INNER_RADIUS, COL_ARC_RED);
#endif
            wasInSweepMode = true;
            _prevFlashSecs = remaining;
            _lastArcSweepMs = now;
        }

        // When second changes, refill the arc
        if (remaining != _prevFlashSecs) {
            _prevFlashSecs = remaining;
            _lastArcSweepMs = now;
            // Draw full red ring at start of each second
#ifdef WOKWI_SIM
            _drawArcSegment(0, 360, COL_ARC_RED);
#else
            ws_fillRing(_gfx, WS_CX, WS_CY, ARC_OUTER_RADIUS, ARC_INNER_RADIUS, COL_ARC_RED);
#endif
        }

        // Calculate how much of the second has elapsed (0.0 to 1.0)
        unsigned long elapsedMs = now - _lastArcSweepMs;
        if (elapsedMs > 1000) elapsedMs = 1000;
        float progress = (float)elapsedMs / 1000.0f;

        // Erase the portion that has "passed" - sweep anticlockwise from 12 o'clock
        // progress=0 means full arc, progress=1 means arc fully erased
        // Erase from (360 - eraseDeg) to 360 so it sweeps anticlockwise
        float eraseDeg = progress * 360.0f;
        if (eraseDeg > 1.0f) {
            float eraseStart = 360.0f - eraseDeg;
#ifdef WOKWI_SIM
            for (float a = eraseStart; a < 360.0f; a += 0.5f) {
                float rad = a * (float)M_PI / 180.0f;
                float s = sinf(rad);
                float c = cosf(rad);
                _tft.drawLine(
                    DISPLAY_CX + (int)(ARC_INNER_RADIUS * s),
                    DISPLAY_CY - (int)(ARC_INNER_RADIUS * c),
                    DISPLAY_CX + (int)(ARC_OUTER_RADIUS * s),
                    DISPLAY_CY - (int)(ARC_OUTER_RADIUS * c),
                    COL_BG);
            }
#else
            _eraseArcSlice(eraseStart, 360.0f);
#endif
        }
        _arcVisible = true;
    } else if (remaining == 0) {
        wasInSweepMode = false;  // Reset for next round
        if (_arcVisible) {
#ifdef WOKWI_SIM
            _drawArc(1, total, COL_BG);
#else
            ws_eraseRingRadial(_gfx, WS_CX, WS_CY, ARC_OUTER_RADIUS, ARC_INNER_RADIUS);
#endif
            _arcVisible = false;
        }
    } else {
        wasInSweepMode = false;  // Reset if we're back above 10s
        // Normal countdown - erase consumed slice and handle color transitions
        if (_prevWtSecs > remaining) {
            // Calculate degrees: remaining time = sweepDeg from 12 o'clock
            float newDeg = (float)remaining / total * 360.0f;
            float oldDeg = (float)_prevWtSecs / total * 360.0f;

            // Check for color threshold crossings
            bool crossedToOrange = (_prevWtSecs > ARC_GREEN_THRESHOLD) && (remaining <= ARC_GREEN_THRESHOLD);
            bool crossedToRed = (_prevWtSecs > ARC_ORANGE_THRESHOLD) && (remaining <= ARC_ORANGE_THRESHOLD);

#ifdef WOKWI_SIM
            // Erase consumed slice
            for (float a = newDeg; a < oldDeg + 0.5f; a += 0.5f) {
                float rad = a * (float)M_PI / 180.0f;
                float s = sinf(rad);
                float c = cosf(rad);
                _tft.drawLine(
                    DISPLAY_CX + (int)(ARC_INNER_RADIUS * s),
                    DISPLAY_CY - (int)(ARC_INNER_RADIUS * c),
                    DISPLAY_CX + (int)(ARC_OUTER_RADIUS * s),
                    DISPLAY_CY - (int)(ARC_OUTER_RADIUS * c),
                    COL_BG);
            }
            // Redraw if color changed
            if (crossedToOrange || crossedToRed) {
                _drawArc(remaining, total, _arcColor(remaining));
            }
#else
            // Erase only the consumed slice (from newDeg to oldDeg)
            _eraseArcSlice(newDeg, oldDeg);

            if (crossedToOrange) {
                // Crossed from green to orange - redraw entire arc orange
                _drawArc(remaining, total, COL_ARC_ORANGE);
            } else if (crossedToRed) {
                // Crossed from orange to red - redraw just the red portion (0 to 30s mark)
                // The portion from 30s onward was already erased, so just draw red
                _drawArc(remaining, total, COL_ARC_RED);
            }
#endif
        }
        _arcVisible = true;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void UI::begin() {
    Serial.printf("[UI] begin  heap=%u\r\n", ESP.getFreeHeap());

#ifdef WOKWI_SIM
    SPI.begin(18, 19, 20, TFT_CS);
    _tft.begin();
    _tft.setRotation(0);

    _tft.fillScreen(ILI9341_RED);
    _tft.fillRect(  0, 0, 60, 60, ILI9341_WHITE);
    _tft.fillRect( 60, 0, 60, 60, ILI9341_GREEN);
    _tft.fillRect(120, 0, 60, 60, ILI9341_BLUE);
    _tft.fillRect(180, 0, 60, 60, ILI9341_YELLOW);
    _tft.setFont(nullptr);
    _tft.setTextSize(3);
    _tft.setTextColor(ILI9341_WHITE, ILI9341_RED);
    _tft.setCursor(10, 80);
    _tft.print("WOKWI OK");
    delay(2000);
    _tft.fillScreen(ILI9341_BLACK);
#else
    // CO5300 QSPI AMOLED - does NOT support hardware rotation
    // Use Arduino_Canvas for software rotation (90° CW)
    _bus = new Arduino_ESP32QSPI(
        LCD_CS,     // CS
        LCD_SCLK,   // SCK
        LCD_SDIO0,  // D0
        LCD_SDIO1,  // D1
        LCD_SDIO2,  // D2
        LCD_SDIO3   // D3
    );

    // Raw display with no rotation
    _display = new Arduino_CO5300(_bus, LCD_RESET, 0 /* rotation */, false /* IPS */,
                                  466, 466,  // Display size
                                  6, 0,      // col_offset1=6, row_offset1=0
                                  0, 0);     // col_offset2=0, row_offset2=0

    // Canvas with software rotation (1 = 90° CW)
    // Framebuffer uses ~424KB - allocated from PSRAM
    _gfx = new Arduino_Canvas(466, 466, _display, 0, 0, 1 /* rotation */);

    if (!_display->begin()) {
        Serial.println("[UI] display init FAILED");
        return;
    }

    if (!_gfx->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.println("[UI] canvas init FAILED");
        return;
    }

    Serial.printf("[UI] canvas allocated, heap=%u\r\n", ESP.getFreeHeap());

    // Clear to black
    _gfx->fillScreen(COL_BG);
    _gfx->flush();

    // Boot splash - green arc ring with text centered
    // Double thick arc (use wider ring)
    ws_fillRing(_gfx, WS_CX, WS_CY, ARC_OUTER_RADIUS + 10, ARC_INNER_RADIUS - 10, COL_GREEN);
    _drawCentered("GLIDE", WS_CX, WS_CY - 20, COL_WHITE, 6);
    _drawCentered("TIMER", WS_CX, WS_CY + 40, COL_WHITE, 3);
    _gfx->flush();
    delay(1500);
    _gfx->fillScreen(COL_BG);
    _gfx->flush();
#endif

    Serial.println("[UI] init done");
}

// ── Main render dispatcher ────────────────────────────────────────────────────

void UI::render(AppState       state,
                const WorkingTime& wt,
                const FlightTimer& ft,
                const FlightLog&   log,
                unsigned long      scratchStartMs,
                int                wtMinutes,
                int                batteryPct,
                bool               isCharging,
                const char*        pilotName,
                BaseConnState      connState,
                int                countdownN,
                int                altitudeM,
                int                altFlightNo,
                int                altTotalFlights,
                bool               isF5K,
                int                timerId,
                int                auxRemainDs,
                int                auxTotalDs,
                bool               jumpedStart,
                int                targetS,
                bool               targetWindow,
                int                pickMin,
                int                pickSec,
                bool               pickWindow,
                bool               pickNone,
                const char*        targetNote,
                int                targetNoteKind)
{
    _targetNote     = targetNote;
    _targetNoteKind = targetNoteKind;
    _jumped = jumpedStart;  // running screen shows JUMPED instead of FLYING
    // Held so every flight-time draw path counts down to the same target without
    // each one needing it threaded through. [TF-10]/[TF-11]
    _targetS      = targetS;
    _targetWindow = targetWindow;
    // The launch in progress is the next one after everything already recorded.
    _launchNo     = log.count() + 1;
    // Treat WORKING_TIME_RUNNING and FLIGHT_RUNNING as the same screen for continuity
    // (arc should NOT reset when starting/stopping a flight)
    bool isRunning     = (state == STATE_WORKING_TIME_RUNNING || state == STATE_FLIGHT_RUNNING);
    bool wasRunning    = (_prevState == STATE_WORKING_TIME_RUNNING || _prevState == STATE_FLIGHT_RUNNING);
    bool stateChanged  = (state != _prevState);
    bool screenChanged = stateChanged && !(isRunning && wasRunning);
    bool connChanged   = (connState != _prevConnState);
    _prevState     = state;
    _prevConnState = connState;

    if (screenChanged) {
        _clearScreen();
        _arcVisible = true;
    }

    switch (state) {
        case STATE_IDLE:
            if (screenChanged || connChanged) {
                if (connChanged && !screenChanged) _clearScreen();
                _drawIdle(connState, pilotName, timerId);
                if (batteryPct >= 0) _drawBattery(batteryPct, isCharging);
                _prevBatteryPct = batteryPct;
            } else if (batteryPct >= 0 && batteryPct != _prevBatteryPct) {
                _drawBattery(batteryPct, isCharging);
                _prevBatteryPct = batteryPct;
            }
            break;

        case STATE_PILOT_SELECT:
            // Always clear and redraw — _needsRender() in main.cpp gates the call rate
            _clearScreen();
            _drawPilotSelect(pilotName ? pilotName : "---");
            break;

        case STATE_WORKING_TIME_RUNNING:
        case STATE_FLIGHT_RUNNING: {
            bool fa   = (state == STATE_FLIGHT_RUNNING);
            int  remS = wt.getRemaining();
            if (screenChanged) _drawRunningFull(fa, wt, ft, log);
            else if (stateChanged) _updateFlightStateOnly(fa, ft, log);  // update flight label/time/log
            else                   _updateRunningInc(fa, wt, ft);
            _prevWtSecs = remS;
            break;
        }

        case STATE_SCRATCH_CONFIRM: {
            int remS = wt.getRemaining();
            if (screenChanged) {
                _drawArc(remS, wt.getTotal(), _arcColor(remS));
                _drawFlightLog(log);
            } else {
                _updateArc(remS, wt.getTotal());
            }
#ifdef WOKWI_SIM
            _tft.fillRoundRect(DISPLAY_CX - 70, 88, 140, 62, 6, C565(0x11, 0x11, 0x11));
            _drawCentered("SCRATCH?",       DISPLAY_CX, 116, COL_RED,   2);
            _drawCentered("B=YES  auto=NO", DISPLAY_CX, 137, COL_WHITE, 1);
            {
                unsigned long el = millis() - scratchStartMs;
                if (el > SCRATCH_CONFIRM_MS) el = SCRATCH_CONFIRM_MS;
                int barW = (int)(120UL * (SCRATCH_CONFIRM_MS - el) / SCRATCH_CONFIRM_MS);
                _tft.fillRect(DISPLAY_CX - 60, 152, 120, 5, COL_DIMGRAY);
                _tft.fillRect(DISPLAY_CX - 60, 152, barW, 5, COL_RED);
            }
#else
            _gfx->fillRoundRect(WS_CX - 100, WS_CY - 50, 200, 100, 10, C565(0x11, 0x11, 0x11));
            _drawCentered("SCRATCH?", WS_CX, WS_CY - 15, COL_RED, 3);
            _drawCentered("B=YES  auto=NO", WS_CX, WS_CY + 20, COL_WHITE, 2);
            {
                unsigned long el = millis() - scratchStartMs;
                if (el > SCRATCH_CONFIRM_MS) el = SCRATCH_CONFIRM_MS;
                int barW = (int)(160UL * (SCRATCH_CONFIRM_MS - el) / SCRATCH_CONFIRM_MS);
                _gfx->fillRect(WS_CX - 80, WS_CY + 50, 160, 8, COL_DIMGRAY);
                _gfx->fillRect(WS_CX - 80, WS_CY + 50, barW, 8, COL_RED);
            }
#endif
            _prevWtSecs = remS;
            break;
        }

        case STATE_WORKING_TIME_EXPIRED:
            if (screenChanged) _drawExpired(log);
            break;

        case STATE_SETTINGS:
            if (screenChanged) _drawSettings(wtMinutes);
            else               _drawSettingsInc(wtMinutes);
            break;

        case STATE_TASK_SELECT:
            if (screenChanged) _drawTaskSelect(isF5K);
            else               _drawTaskSelectInc(isF5K);
            break;

        case STATE_ALTITUDE_ENTRY:
            // Always full redraw — ensures number updates even if incremental
            // clear region doesn't match the font's actual bounding box.
            if (!screenChanged) _clearScreen();
            _drawAltitudeEntry(altitudeM, altFlightNo, altTotalFlights);
            _prevAltFlightNo = altFlightNo;
            break;

        case STATE_TARGET_SET:
            // Full redraw every time, like altitude entry: the value changes shape
            // between "---", "W" and "M:SS", so an incremental clear region cannot
            // be right for all three. [TF-10]
            if (!screenChanged) _clearScreen();
            _drawTargetPicker(pickMin, pickSec, pickWindow, pickNone,
                              ft.isRunning() ? ft.elapsed() : 0);
            break;

        case STATE_COUNTDOWN: {
            // Clear on every render (called only when n changes — once per second)
            if (!screenChanged) _clearScreen();
            if (countdownN > 0) {
                // Anticlockwise green arc: full at 10, shrinks from the clockwise side of 12
                float startDeg = (10 - countdownN) * 36.0f;
                _drawArcSegment(startDeg, 360.0f, COL_ARC_GREEN);
            }
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%d", countdownN);
#ifdef WOKWI_SIM
                _drawCentered(buf,        DISPLAY_CX, DISPLAY_CY - 20, COL_WHITE, 8);
                _drawCentered("GET READY",DISPLAY_CX, DISPLAY_CY + 40, COL_GREEN, 2);
#else
                _drawFontCentered(buf,         WS_CX, WS_CY - 20, COL_WHITE,     &FreeSansBold24pt7b);
                _drawFontCentered("GET READY", WS_CX, WS_CY + 70, COL_ARC_GREEN, &FreeSans12pt7b);
#endif
            }
            break;
        }

        case STATE_PREP:
            // Called ten times a second (gated on tenths by _needsRender). Only fall
            // back to the expensive full redraw when the layout actually changes:
            // entering the screen, crossing the 10 s boundary, or the base re-syncing
            // the clock backwards — otherwise repaint just the number and arc slice.
            if (screenChanged || _prevPrepDs < 0 || auxRemainDs > _prevPrepDs ||
                (auxRemainDs > 100) != (_prevPrepDs > 100)) {
                if (!screenChanged) _clearScreen();
                _drawPrep(auxRemainDs, auxTotalDs, pilotName);
            } else {
                _drawPrepInc(auxRemainDs, _prevPrepDs, auxTotalDs);
            }
            _prevPrepDs = auxRemainDs;
            break;

        case STATE_LANDING:
            if (!screenChanged) _clearScreen();
            _drawLanding(auxRemainDs, auxTotalDs);
            break;
    }

#ifndef WOKWI_SIM
    // Push canvas framebuffer to display
    _gfx->flush();
#endif
}

// ── Battery indicator ─────────────────────────────────────────────────────────

void UI::_drawBattery(int pct, bool charging) {
#ifndef WOKWI_SIM
    // Classic battery icon at top center (near 12 o'clock)
    // Horizontal battery shape with terminal nub on right
    const int BODY_W = 40;    // Main body width
    const int BODY_H = 18;    // Main body height
    const int TERM_W = 4;     // Terminal nub width
    const int TERM_H = 8;     // Terminal nub height
    const int BORDER = 2;     // Border thickness
    const int INNER_PAD = 2;  // Padding inside border

    const int BODY_X = WS_CX - (BODY_W + TERM_W) / 2;
    const int BODY_Y = 45;

    // Clear area
    _gfx->fillRect(BODY_X - 2, BODY_Y - 2, BODY_W + TERM_W + 4, BODY_H + 4, COL_BG);

    // Battery body outline (rounded corners simulated with filled rects)
    // Outer border
    _gfx->fillRoundRect(BODY_X, BODY_Y, BODY_W, BODY_H, 3, COL_WHITE);
    // Inner cutout (black)
    _gfx->fillRoundRect(BODY_X + BORDER, BODY_Y + BORDER,
                        BODY_W - BORDER * 2, BODY_H - BORDER * 2, 2, COL_BG);

    // Terminal nub on right side
    int termX = BODY_X + BODY_W;
    int termY = BODY_Y + (BODY_H - TERM_H) / 2;
    _gfx->fillRoundRect(termX, termY, TERM_W + 2, TERM_H, 2, COL_WHITE);

    // Fill area dimensions (inside the border)
    int fillAreaX = BODY_X + BORDER + INNER_PAD;
    int fillAreaY = BODY_Y + BORDER + INNER_PAD;
    int fillAreaW = BODY_W - BORDER * 2 - INNER_PAD * 2;
    int fillAreaH = BODY_H - BORDER * 2 - INNER_PAD * 2;

    // Calculate fill width based on percentage
    int fillW = (pct * fillAreaW) / 100;
    if (fillW < 0) fillW = 0;
    if (fillW > fillAreaW) fillW = fillAreaW;

    // Fill color based on level
    uint16_t fillCol;
    if (pct <= 15) {
        fillCol = COL_RED;
    } else if (pct <= 30) {
        fillCol = COL_ORANGE;
    } else {
        fillCol = COL_GREEN;
    }

    // Draw fill from left
    if (fillW > 0) {
        _gfx->fillRect(fillAreaX, fillAreaY, fillW, fillAreaH, fillCol);
    }

    // Charging indicator: lightning bolt symbol overlaid
    if (charging) {
        // Simple lightning bolt in center of battery
        int boltX = BODY_X + BODY_W / 2;
        int boltY = BODY_Y + BODY_H / 2;
        // Draw a small yellow lightning bolt shape
        _gfx->fillTriangle(boltX - 2, boltY - 5, boltX + 4, boltY - 5, boltX, boltY + 1, COL_YELLOW);
        _gfx->fillTriangle(boltX - 4, boltY - 1, boltX + 2, boltY - 1, boltX - 2, boltY + 5, COL_YELLOW);
    }
#endif
}

// ── Idle ──────────────────────────────────────────────────────────────────────

void UI::_drawIdle(BaseConnState connState, const char* pilotName, int timerId) {
#ifdef WOKWI_SIM
    _drawCentered("GLIDE",            DISPLAY_CX, 100, COL_WHITE, 5);
    _drawCentered("TIMER",            DISPLAY_CX, 152, COL_GRAY,  2);
    _drawCentered("R = FLY + WT",     DISPLAY_CX, 200, COL_WHITE, 1);
    _drawCentered("L = WT ONLY",      DISPLAY_CX, 218, COL_WHITE, 1);
    _drawCentered("R(hold) = SET",    DISPLAY_CX, 236, COL_GRAY, 1);
#else
    // Subtle outer ring as decorative bezel
    ws_fillRing(_gfx, WS_CX, WS_CY, 225, 218, COL_DIMGRAY);

    // Timer ID — shown between battery and GLIDE when base has assigned an ID
    if (timerId >= 0) {
        char tidBuf[6];
        snprintf(tidBuf, sizeof(tidBuf), "T%d", timerId);
        _drawFontCentered(tidBuf, WS_CX, 130, COL_GREEN, &FreeSansBold24pt7b);
    }

    // Main title
    _drawFontCentered("GLIDE", WS_CX, 190, COL_WHITE, &FreeSansBold24pt7b);
    _drawFontCentered("TIMER", WS_CX, 240, COL_GRAY, &FreeSansBold18pt7b);

    // Hints
    _drawFontCentered("R = FLY + WT", WS_CX, 310, COL_WHITE, &FreeSans12pt7b);
    _drawFontCentered("L = WT ONLY", WS_CX, 340, COL_WHITE, &FreeSans12pt7b);
    _drawFontCentered("R(hold) = SET", WS_CX, 385, COL_DIMGRAY, &FreeSans9pt7b);

    // Base station status at bottom
    if (connState == BASE_CONNECTED) {
        const char* label = (pilotName && pilotName[0]) ? pilotName : "BASE OK";
        _drawFontCentered(label, WS_CX, 415, COL_GREEN, &FreeSans9pt7b);
    } else if (connState == BASE_CONNECTING) {
        _drawFontCentered("BASE...", WS_CX, 415, COL_DIMGRAY, &FreeSans9pt7b);
    }
#endif
}

void UI::_drawPilotSelect(const char* pilotName) {
#ifdef WOKWI_SIM
    _drawCentered("SELECT PILOT", DISPLAY_CX, 60,  COL_GRAY,  1);
    _drawCentered(pilotName,      DISPLAY_CX, 140, COL_WHITE, 2);
    _drawCentered("R=NEXT L=PREV",DISPLAY_CX, 200, COL_WHITE, 1);
    _drawCentered("HOLD R=CONFIRM",DISPLAY_CX,220, COL_GRAY,  1);
#else
    _gfx->fillCircle(WS_CX, WS_CY, 233, COL_BG);

    _drawFontCentered("SELECT PILOT", WS_CX, 120, COL_GRAY, &FreeSans12pt7b);

    // Pilot name large in centre zone
    _drawFontCentered(pilotName, WS_CX, 233, COL_WHITE, &FreeSansBold18pt7b);

    // Navigation hints
    _drawFontCentered("R = NEXT   L = PREV", WS_CX, 330, COL_WHITE, &FreeSans9pt7b);
    _drawFontCentered("HOLD R = CONFIRM",    WS_CX, 370, COL_DIMGRAY, &FreeSans9pt7b);
#endif
}

// ── Running — full draw ───────────────────────────────────────────────────────

void UI::_drawRunningFull(bool flightActive,
                          const WorkingTime& wt,
                          const FlightTimer& ft,
                          const FlightLog&   log)
{
    int remS = wt.getRemaining();
    unsigned long remMs = wt.getRemainingMs();
    _drawArc(remS, wt.getTotal(), _arcColor(remS));

    char buf[16];
    uint16_t col = (remS <= ARC_RED_THRESHOLD) ? COL_RED : COL_WHITE;

#ifdef WOKWI_SIM
    _drawCentered("WORKING TIME", DISPLAY_CX, Y_WT_LABEL, COL_GRAY, 1);
    _tft.drawFastHLine(DISPLAY_CX - DIV_HALF, Y_DIV1, DIV_HALF * 2, COL_DIMGRAY);

    _drawCentered("FLIGHT TIME", DISPLAY_CX, Y_FL_LABEL, COL_GRAY, 1);
    _tft.drawFastHLine(DISPLAY_CX - DIV_HALF, Y_DIV2, DIV_HALF * 2, COL_DIMGRAY);

    _drawCentered(fmtMs(remMs, buf, sizeof(buf)), DISPLAY_CX, Y_WT_DIGITS, col, 3);

    unsigned long el = ft.elapsed();
    col = _flightShowCol(el, flightActive);
    _drawCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)), DISPLAY_CX, Y_FL_DIGITS, col, 2);

    _drawFlightLog(log);
#else
    // Flight time (large) at top - primary focus for caller
    _drawFontCentered("FLIGHT TIME", WS_CX, WS_Y_FL_LABEL, COL_GRAY, &FreeSans12pt7b);

    unsigned long el = ft.elapsed();
    uint16_t flCol = _flightShowCol(el, flightActive);
    _drawFontCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)), WS_CX, WS_Y_FL_DIGITS, flCol, &FreeMonoBold24pt7b);

    // State indicator (FLYING/JUMPED/WAIT) in middle
    _drawFontCentered(_stateLabel(flightActive), WS_CX, WS_Y_STATE,
                      _stateLabelCol(flightActive),
                      &FreeSansBold18pt7b);

    // Flight log (3 best times) in middle
    _drawFlightLog(log);

    // Working time at bottom (smaller)
    _drawFontCentered(fmtMs(remMs, buf, sizeof(buf)), WS_CX, WS_Y_WT_DIGITS, col, &FreeMonoBold18pt7b);
#endif
}

// ── Running — incremental ─────────────────────────────────────────────────────

void UI::_updateRunningInc(bool flightActive,
                           const WorkingTime& wt,
                           const FlightTimer& ft)
{
    int remS = wt.getRemaining();
    unsigned long remMs = wt.getRemainingMs();
    _updateArc(remS, wt.getTotal());

    char buf[16];
    uint16_t col = (remS <= ARC_RED_THRESHOLD) ? COL_RED : COL_WHITE;

    // ⚠ This is the ONLY path that redraws the flight clock while a glider is in
    // the air (every 50 ms), so it must count down to the target exactly like the
    // full-redraw paths. Drawing `ft.elapsed()` raw here is what made the Poker
    // countdown invisible on hardware even though _flightShowMs() was correct.
    unsigned long el = ft.elapsed();

#ifdef WOKWI_SIM
    _drawCentered(fmtMs(remMs, buf, sizeof(buf)), DISPLAY_CX, Y_WT_DIGITS, col, 3);
    if (flightActive) {
        _drawCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)),
                      DISPLAY_CX, Y_FL_DIGITS, _flightShowCol(el, flightActive), 2);
    }
#else
    // Update working time at bottom
    _drawFontCentered(fmtMs(remMs, buf, sizeof(buf)), WS_CX, WS_Y_WT_DIGITS, col, &FreeMonoBold18pt7b);
    // Update flight time at top if active
    if (flightActive) {
        _drawFontCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)),
                          WS_CX, WS_Y_FL_DIGITS, _flightShowCol(el, flightActive),
                          &FreeMonoBold24pt7b);
    }
#endif
}

// ── Running — flight state change only (no arc reset) ─────────────────────────

void UI::_updateFlightStateOnly(bool flightActive, const FlightTimer& ft, const FlightLog& log) {
    char buf[16];

#ifdef WOKWI_SIM
    // Update flight label (always "FLIGHT TIME")
    _tft.fillRect(DISPLAY_CX - 70, Y_FL_LABEL - 5, 140, 14, COL_BG);
    _drawCentered("FLIGHT TIME", DISPLAY_CX, Y_FL_LABEL, COL_GRAY, 1);

    // Update flight time
    unsigned long el = ft.elapsed();
    uint16_t col = _flightShowCol(el, flightActive);
    _tft.fillRect(DISPLAY_CX - 50, Y_FL_DIGITS - 12, 100, 30, COL_BG);
    _drawCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)), DISPLAY_CX, Y_FL_DIGITS, col, 3);

    // Redraw flight log (new flight may have been recorded)
    _tft.fillRect(DISPLAY_CX - 80, Y_LOG_START - 5, 160, Y_LOG_STEP * 3 + 10, COL_BG);
    _drawFlightLog(log);
#else
    // Update flight label at top (always "FLIGHT TIME")
    _gfx->fillRect(WS_CX - 110, WS_Y_FL_LABEL - 20, 220, 40, COL_BG);
    _drawFontCentered("FLIGHT TIME", WS_CX, WS_Y_FL_LABEL, COL_GRAY, &FreeSans12pt7b);

    // Update flight time (large) at top
    unsigned long el = ft.elapsed();
    uint16_t col = _flightShowCol(el, flightActive);
    _gfx->fillRect(WS_CX - 120, WS_Y_FL_DIGITS - 35, 240, 70, COL_BG);
    _drawFontCentered(fmtMs(_flightShowMs(el), buf, sizeof(buf)), WS_CX, WS_Y_FL_DIGITS, col, &FreeMonoBold24pt7b);

    // Update state label (FLYING/JUMPED/WAIT) — only changes when flight state changes
    _gfx->fillRect(WS_CX - 100, WS_Y_STATE - 25, 200, 50, COL_BG);
    _drawFontCentered(_stateLabel(flightActive), WS_CX, WS_Y_STATE,
                      _stateLabelCol(flightActive),
                      &FreeSansBold18pt7b);

    // Redraw flight log (new flight may have been recorded)
    _gfx->fillRect(WS_CX - 100, WS_Y_LOG_START - 10, 200, WS_Y_LOG_STEP * 3 + 20, COL_BG);
    _drawFlightLog(log);
#endif
}

// ── Expired ───────────────────────────────────────────────────────────────────

void UI::_drawExpired(const FlightLog& log) {
#ifdef WOKWI_SIM
    _drawCentered("TIME UP", DISPLAY_CX, 95, COL_RED, 3);
    _tft.drawFastHLine(DISPLAY_CX - DIV_HALF, 115, DIV_HALF * 2, COL_DIMGRAY);
    _drawFlightLogExpired(log, 130, 6);
    _drawCentered("A = RESTART", DISPLAY_CX, 258, COL_GRAY, 1);
#else
    // TIME UP on one line at top
    _drawFontCentered("TIME UP", WS_CX, 80, COL_RED, &FreeSansBold18pt7b);
    // All flight times below - includes scratched in red with strikethrough
    _drawFlightLogExpired(log, 130, 8);
    _drawFontCentered("R = RESTART", WS_CX, 420, COL_GRAY, &FreeSans12pt7b);
#endif
}

// ── Prep countdown (base-driven, yellow arc) ─────────────────────────────────

// Single monospace string: "M:SS.T" above 10 s, "N.T" in the final 10 s.
// Using FreeMonoBold throughout means every character cell is the same width,
// so drawing a new string at the same position cleanly overwrites the old one
// (setTextColor fills each cell background) with no ghost pixels.
static void _fmtPrep(int remainDs, char* buf, size_t len) {
    if (remainDs < 0) remainDs = 0;
    if (remainDs > 100) {
        int s = remainDs / 10;
        snprintf(buf, len, "%d:%02d.%d", s / 60, s % 60, remainDs % 10);
    } else {
        snprintf(buf, len, "%d.%d", remainDs / 10, remainDs % 10);
    }
}

void UI::_drawPrep(int remainDs, int totalDs, const char* pilotName) {
    _drawArc(remainDs, totalDs, COL_YELLOW);
    char buf[12];
    _fmtPrep(remainDs, buf, sizeof(buf));
    if (remainDs > 100) {
#ifdef WOKWI_SIM
        _drawCentered("PREP TIME", DISPLAY_CX, DISPLAY_CY - 50, COL_YELLOW, 2);
        _drawCentered(buf,         DISPLAY_CX, DISPLAY_CY,      COL_WHITE,  4);
        if (pilotName && pilotName[0])
            _drawCentered(pilotName, DISPLAY_CX, DISPLAY_CY + 50, COL_GRAY, 1);
#else
        _drawFontCentered("PREP TIME", WS_CX, 130, COL_YELLOW, &FreeSansBold18pt7b);
        _drawFontCentered(buf, WS_CX, WS_CY + 10, COL_WHITE, &FreeMonoBold24pt7b, 1);
        if (pilotName && pilotName[0])
            _drawFontCentered(pilotName, WS_CX, 360, COL_GRAY, &FreeSans12pt7b);
#endif
    } else {
        // Final 10 s — big centered time, no labels: pilot is focused on the clock
#ifdef WOKWI_SIM
        _drawCentered(buf, DISPLAY_CX, DISPLAY_CY, COL_WHITE, 6);
#else
        _drawFontCentered(buf, WS_CX, WS_CY, COL_WHITE, &FreeMonoBold24pt7b, 2);
#endif
    }
}

void UI::_drawPrepInc(int remainDs, int prevDs, int totalDs) {
#ifndef WOKWI_SIM
    // Erase consumed arc slice
    if (totalDs > 0 && prevDs > remainDs) {
        _eraseArcSlice((float)remainDs / totalDs * 360.0f,
                       (float)prevDs   / totalDs * 360.0f);
    }
    char buf[12];
    _fmtPrep(remainDs, buf, sizeof(buf));
    if (remainDs > 100) {
        // "M:SS.T" is always 6 monospace chars. setTextColor(fg, bg) fills each
        // character cell so re-drawing at the same position overwrites cleanly.
        _drawFontCentered(buf, WS_CX, WS_CY + 10, COL_WHITE, &FreeMonoBold24pt7b, 1);
    } else {
        // "N.T" can be 3 chars ("9.4") or 4 chars ("10.0"), so clear first to
        // prevent ghost pixels when the string shortens at the 10→9 boundary.
        _gfx->fillRect(WS_CX - 110, WS_CY - 52, 220, 84, COL_BG);
        _drawFontCentered(buf, WS_CX, WS_CY, COL_WHITE, &FreeMonoBold24pt7b, 2);
    }
#endif
}

// ── Landing window countdown (base-driven, orange arc) ───────────────────────

void UI::_drawLanding(int remainDs, int totalDs) {
    _drawArc(remainDs, totalDs, COL_ORANGE);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", remainDs / 10);
#ifdef WOKWI_SIM
    _drawCentered("LANDING", DISPLAY_CX, DISPLAY_CY - 60, COL_ORANGE, 2);
    _drawCentered(buf,       DISPLAY_CX, DISPLAY_CY + 10, COL_WHITE,  8);
#else
    _drawFontCentered("LANDING", WS_CX, 120, COL_ORANGE, &FreeSansBold18pt7b);
    _drawFontCentered(buf, WS_CX, WS_CY + 20, COL_WHITE, &FreeSansBold24pt7b, 3);
#endif
}

// ── Altitude entry (F5K) ─────────────────────────────────────────────────────

void UI::_drawAltitudeEntry(int altM, int flightNo, int totalFlights) {
    char altBuf[8], hdrBuf[24];
    snprintf(altBuf, sizeof(altBuf), "%03d", altM);
    snprintf(hdrBuf, sizeof(hdrBuf), "FLIGHT %d of %d", flightNo, totalFlights);

#ifdef WOKWI_SIM
    _drawCentered(hdrBuf,           DISPLAY_CX, 35,  COL_YELLOW, 1);
    _drawCentered("ALTITUDE",       DISPLAY_CX, 60,  COL_GRAY,   1);
    _drawCentered(altBuf,           DISPLAY_CX, 150, COL_WHITE,  5);
    _drawCentered("m",              DISPLAY_CX, 195, COL_GRAY,   2);
    _drawCentered("L=+10  R=+1",   DISPLAY_CX, 245, COL_WHITE,  1);
    _drawCentered("HOLD R = OK",   DISPLAY_CX, 265, COL_GRAY,   1);
#else
    _drawFontCentered(hdrBuf,                  WS_CX, 75,  COL_ORANGE,    &FreeSans12pt7b);
    _drawFontCentered("ALTITUDE",              WS_CX, 120, COL_GRAY,      &FreeSans9pt7b);
    _drawFontCentered(altBuf,                  WS_CX, 240, COL_WHITE,     &FreeMonoBold24pt7b);
    _drawFontCentered("m",                     WS_CX, 300, COL_GRAY,      &FreeSansBold18pt7b);
    _drawFontCentered("L = +10m   R = +1m",    WS_CX, 370, COL_WHITE,     &FreeSans12pt7b);
    _drawFontCentered("HOLD R = CONFIRM",      WS_CX, 408, COL_DIMGRAY,   &FreeSans9pt7b);
#endif
}

const char* UI::_stateLabel(bool flightActive) const {
    if (_targetNote) return _targetNote;
    if (!flightActive) return "WAIT";
    // The launch number rides with the label because that is the only line on the
    // running screen the timekeeper is already watching, and on a launch-limited
    // task (F3K F: six, F5K A: four, F5K B/D/E: three) it is what they need. A
    // jumped start still consumes a launch, so it is numbered too. [I-46]/[I-49]
    snprintf(_stateLabelBuf, sizeof(_stateLabelBuf), "%s (%d)",
             _jumped ? "JUMPED" : "FLYING", _launchNo);
    return _stateLabelBuf;
}

uint16_t UI::_stateLabelCol(bool flightActive) const {
    if (_targetNote) {
        switch (_targetNoteKind) {
            case TARGET_NOTE_ACHIEVED: return COL_ARC_GREEN;
            case TARGET_NOTE_MISSED:   return COL_RED;
            case TARGET_NOTE_WINDOW:   return COL_ORANGE;
            default:                   return COL_YELLOW;
        }
    }
    return flightActive ? (_jumped ? COL_RED : COL_GREEN) : COL_GRAY;
}

unsigned long UI::_flightShowMs(unsigned long elapsedMs) const {
    if (_targetS <= 0) return elapsedMs;
    const unsigned long target = (unsigned long)_targetS * 1000UL;
    return (elapsedMs >= target) ? (elapsedMs - target) : (target - elapsedMs);
}

uint16_t UI::_flightShowCol(unsigned long elapsedMs, bool flightActive) const {
    if (!flightActive) return elapsedMs > 0 ? COL_WHITE : COL_DIMGRAY;
    if (_targetS <= 0) return COL_GREEN;
    // Orange past the target: the call is banked and this is overtime, which is
    // wasted airtime in Poker. Distinguishable at a glance from counting down.
    return (elapsedMs >= (unsigned long)_targetS * 1000UL) ? COL_ORANGE : COL_GREEN;
}

void UI::_drawTargetPicker(int mins, int secs, bool isWindow, bool isNone,
                           unsigned long flightMs) {
    char valBuf[12], ftBuf[20];
    if (isNone)        snprintf(valBuf, sizeof(valBuf), "---");
    else if (isWindow) snprintf(valBuf, sizeof(valBuf), "W");
    else               snprintf(valBuf, sizeof(valBuf), "%d:%02d", mins, secs);

    // The running flight, shown because declaring AFTER the launch is explicitly
    // permitted (FAI 2025 F3K.11.5) and is the quick turn-around case. Kris:
    // "you need to show the active FT if it is running (sanity for the timer)".
    const bool flying = flightMs > 0;
    if (flying) {
        unsigned long t = flightMs / 100;    // tenths
        snprintf(ftBuf, sizeof(ftBuf), "FLYING %lu:%02lu.%lu",
                 t / 600, (t / 10) % 60, t % 10);
    } else {
        snprintf(ftBuf, sizeof(ftBuf), "NOT LAUNCHED");
    }

#ifdef WOKWI_SIM
    _drawCentered("CALL TARGET",  DISPLAY_CX, 35,  COL_YELLOW, 1);
    _drawCentered(ftBuf,          DISPLAY_CX, 60,  flying ? COL_GREEN : COL_GRAY, 1);
    _drawCentered(valBuf,         DISPLAY_CX, 150, COL_WHITE,  5);
    _drawCentered("L=W/MIN R=+5s", DISPLAY_CX, 235, COL_WHITE, 1);
    _drawCentered("HOLD R 2s = OK", DISPLAY_CX, 258, COL_GRAY, 1);
#else
    _drawFontCentered("CALL TARGET",          WS_CX, 75,  COL_ORANGE, &FreeSans12pt7b);
    _drawFontCentered(ftBuf,                  WS_CX, 120,
                      flying ? COL_ARC_GREEN : COL_GRAY,             &FreeSans9pt7b);
    _drawFontCentered(valBuf,                 WS_CX, 245, COL_WHITE,  &FreeMonoBold24pt7b);
    // Three R gestures, so they have to be spelled out. The long-press +1s exists
    // only because the rulebook's own example call is 2:38.
    _drawFontCentered("L = W / MIN    R = +5s", WS_CX, 340, COL_WHITE, &FreeSans12pt7b);
    _drawFontCentered("R LONG = +1s     HOLD R 2s = OK",
                                              WS_CX, 378, COL_DIMGRAY, &FreeSans9pt7b);
    // The two ends of the wrap, spelled out — neither is guessable, and there is
    // no L-hold to give either its own gesture.
    _drawFontCentered("--- = no call    W = rest of window",
                                              WS_CX, 408, COL_DIMGRAY, &FreeSans9pt7b);
#endif
}

void UI::_drawAltitudeEntryInc(int altM) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%03d", altM);

#ifdef WOKWI_SIM
    _tft.fillRect(DISPLAY_CX - 55, 115, 110, 55, COL_BG);
    _drawCentered(buf, DISPLAY_CX, 150, COL_WHITE, 5);
#else
    _gfx->fillRect(WS_CX - 100, 195, 200, 70, COL_BG);
    _drawFontCentered(buf, WS_CX, 240, COL_WHITE, &FreeMonoBold24pt7b);
#endif
}

// ── Settings ──────────────────────────────────────────────────────────────────

void UI::_drawSettings(int minutes) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", minutes);

#ifdef WOKWI_SIM
    _drawCentered("SET WORKING TIME", DISPLAY_CX, 35, COL_GRAY, 1);
    _tft.drawFastHLine(20, 47, 200, C565(0x33, 0x33, 0x33));
    _drawCentered(buf,   DISPLAY_CX, 108, COL_WHITE, 5);
    _drawCentered("min", DISPLAY_CX, 146, COL_GRAY, 2);
    _tft.drawFastHLine(20, 165, 200, C565(0x33, 0x33, 0x33));

    // Presets
    const int xs[3] = {50, 120, 190};
    const int presets[3] = {3, 5, 10};
    const char* labels[3] = {"3", "5", "10"};
    for (int i = 0; i < 3; i++) {
        uint16_t col = (minutes == presets[i]) ? COL_GREEN : COL_DIMGRAY;
        _drawCentered(labels[i], xs[i], 185, col, 2);
    }
    _tft.setTextSize(1);
    _tft.setTextColor(COL_DIMGRAY, COL_BG);
    _tft.setCursor(44, 204); _tft.print("min");
    _tft.setCursor(113, 204); _tft.print("min");
    _tft.setCursor(183, 204); _tft.print("min");

    _tft.drawFastHLine(20, 218, 200, C565(0x33, 0x33, 0x33));
    _drawCentered("A = +1 min",       DISPLAY_CX, 232, COL_WHITE, 1);
    _drawCentered("B = -1 min",       DISPLAY_CX, 248, COL_WHITE, 1);
    _drawCentered("hold A = confirm", DISPLAY_CX, 272, COL_GRAY, 1);
    _drawCentered("(auto 8s)",        DISPLAY_CX, 288, COL_DIMGRAY, 1);
#else
    _drawFontCentered("SET WORKING TIME", WS_CX, 100, COL_GRAY, &FreeSans12pt7b);
    _drawFontCentered(buf, WS_CX, WS_CY, COL_WHITE, &FreeMonoBold24pt7b);
    _drawFontCentered("min", WS_CX, WS_CY + 50, COL_GRAY, &FreeSansBold18pt7b);

    // Presets in a row
    const int presets[3] = {3, 5, 10};
    const char* labels[3] = {"3", "5", "10"};
    int spacing = 100;
    for (int i = 0; i < 3; i++) {
        int x = WS_CX + (i - 1) * spacing;
        uint16_t col = (minutes == presets[i]) ? COL_GREEN : COL_DIMGRAY;
        _drawFontCentered(labels[i], x, WS_CY + 110, col, &FreeSansBold18pt7b);
    }

    _drawFontCentered("R = +1 min", WS_CX, WS_CY + 160, COL_WHITE, &FreeSans12pt7b);
    _drawFontCentered("L = -1 min", WS_CX, WS_CY + 190, COL_WHITE, &FreeSans12pt7b);
#endif
}

void UI::_drawSettingsInc(int minutes) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", minutes);

#ifdef WOKWI_SIM
    _tft.fillRect(DISPLAY_CX - 55, 80, 110, 55, COL_BG);
    _drawCentered(buf, DISPLAY_CX, 108, COL_WHITE, 5);

    const int xs[3] = {50, 120, 190};
    const int presets[3] = {3, 5, 10};
    const char* labels[3] = {"3", "5", "10"};
    for (int i = 0; i < 3; i++) {
        uint16_t col = (minutes == presets[i]) ? COL_GREEN : COL_DIMGRAY;
        _drawCentered(labels[i], xs[i], 185, col, 2);
    }
#else
    _gfx->fillRect(WS_CX - 80, WS_CY - 30, 160, 60, COL_BG);
    _drawFontCentered(buf, WS_CX, WS_CY, COL_WHITE, &FreeMonoBold24pt7b);

    // Clear and redraw presets
    _gfx->fillRect(WS_CX - 160, WS_CY + 85, 320, 50, COL_BG);
    const int presets[3] = {3, 5, 10};
    const char* labels[3] = {"3", "5", "10"};
    int spacing = 100;
    for (int i = 0; i < 3; i++) {
        int x = WS_CX + (i - 1) * spacing;
        uint16_t col = (minutes == presets[i]) ? COL_GREEN : COL_DIMGRAY;
        _drawFontCentered(labels[i], x, WS_CY + 110, col, &FreeSansBold18pt7b);
    }
#endif
}

// ── Task select (settings page 2) ────────────────────────────────────────────

void UI::_drawTaskSelect(bool isF5K) {
    const char* label = isF5K ? "F5K" : "F3K";
#ifdef WOKWI_SIM
    _drawCentered("TASK TYPE",       DISPLAY_CX, 35,  COL_GRAY,    1);
    _drawCentered(label,             DISPLAY_CX, 150, COL_WHITE,   5);
    _drawCentered("R/L = TOGGLE",    DISPLAY_CX, 235, COL_WHITE,   1);
    _drawCentered("hold R = confirm", DISPLAY_CX, 255, COL_GRAY,   1);
    _drawCentered("(auto 8s)",        DISPLAY_CX, 275, COL_DIMGRAY, 1);
#else
    _drawFontCentered("TASK TYPE",       WS_CX, 100, COL_GRAY,      &FreeSans12pt7b);
    _drawFontCentered(label,             WS_CX, WS_CY, COL_WHITE,   &FreeSansBold24pt7b);
    _drawFontCentered("R / L  =  TOGGLE", WS_CX, 370, COL_WHITE,   &FreeSans12pt7b);
    _drawFontCentered("HOLD R = OK",     WS_CX, 408, COL_DIMGRAY,  &FreeSans9pt7b);
#endif
}

void UI::_drawTaskSelectInc(bool isF5K) {
    const char* label = isF5K ? "F5K" : "F3K";
#ifdef WOKWI_SIM
    _tft.fillRect(DISPLAY_CX - 55, 115, 110, 55, COL_BG);
    _drawCentered(label, DISPLAY_CX, 150, COL_WHITE, 5);
#else
    _gfx->fillRect(WS_CX - 80, WS_CY - 50, 160, 80, COL_BG);
    _drawFontCentered(label, WS_CX, WS_CY, COL_WHITE, &FreeSansBold24pt7b);
#endif
}

// ── Flight log ────────────────────────────────────────────────────────────────

void UI::_drawFlightLog(const FlightLog& log, int startY, int maxShown) {
#ifdef WOKWI_SIM
    if (startY == 0) startY = Y_LOG_START;
    int step = Y_LOG_STEP;
#else
    if (startY == 0) startY = WS_Y_LOG_START;
    int step = WS_Y_LOG_STEP;
#endif

    int total = log.count();
    int best  = log.bestIndex();

    // Find top 3 best times (longest valid flights), keep in rank order (best first)
    int  selected[3];
    bool used[MAX_FLIGHTS];
    for (int i = 0; i < MAX_FLIGHTS; i++) used[i] = false;
    int selCount = 0;

    for (int pick = 0; pick < maxShown && pick < total; pick++) {
        int topIdx = -1;
        for (int i = 0; i < total; i++) {
            if (used[i]) continue;
            Flight f = log.get(i);
            if (f.scratched || f.durationMs == 0) continue;
            if (topIdx < 0 || f.durationMs > log.get(topIdx).durationMs)
                topIdx = i;
        }
        if (topIdx < 0) break;
        selected[selCount++] = topIdx;
        used[topIdx] = true;
    }

    // Display in rank order (best first) with flight number
    // Traffic light: 1st=green, 2nd=orange, 3rd=yellow
    for (int slot = 0; slot < selCount; slot++) {
        int    flightIdx = selected[slot];
        Flight f = log.get(flightIdx);
        int    y = startY + slot * step;

        char timeBuf[16]; fmtMs(f.durationMs, timeBuf, sizeof(timeBuf));
        char row[32];
        snprintf(row, sizeof(row), "%d. %s", flightIdx + 1, timeBuf);

        uint16_t col;
        if (slot == 0)      col = COL_GREEN;
        else if (slot == 1) col = COL_ORANGE;
        else                col = COL_YELLOW;
#ifdef WOKWI_SIM
        _drawCentered(row, DISPLAY_CX, y, col, 1);
#else
        _drawCentered(row, WS_CX, y, col, 3);
#endif
    }
}

// Flight log for expired screen - shows ALL flights including scratched ones
// Top 3 valid flights are marked with asterisk and yellow text
void UI::_drawFlightLogExpired(const FlightLog& log, int startY, int maxShown) {
#ifdef WOKWI_SIM
    int step = Y_LOG_STEP;
#else
    int step = 38;  // Spacing for FreeMonoBold18pt font
#endif

    int total = log.count();

    // Find top 3 best (longest) valid flights
    int top3[3] = {-1, -1, -1};
    for (int pick = 0; pick < 3; pick++) {
        int bestIdx = -1;
        unsigned long bestMs = 0;
        for (int i = 0; i < total; i++) {
            Flight f = log.get(i);
            if (f.scratched || f.durationMs == 0) continue;
            // Skip if already selected
            bool alreadyPicked = false;
            for (int p = 0; p < pick; p++) {
                if (top3[p] == i) { alreadyPicked = true; break; }
            }
            if (alreadyPicked) continue;
            if (f.durationMs > bestMs) {
                bestMs = f.durationMs;
                bestIdx = i;
            }
        }
        top3[pick] = bestIdx;
    }

    int shown = 0;
    for (int i = 0; i < total && shown < maxShown; i++) {
        Flight f = log.get(i);
        if (f.durationMs == 0) continue;

        // Check if this flight is in top 3 and which rank
        int rank = -1;  // 0=best, 1=2nd, 2=3rd
        if (!f.scratched) {
            for (int p = 0; p < 3; p++) {
                if (top3[p] == i) { rank = p; break; }
            }
        }

        int y = startY + shown * step;
        char timeBuf[16]; fmtMs(f.durationMs, timeBuf, sizeof(timeBuf));
        char row[32];
        snprintf(row, sizeof(row), "%d. %s%s", i + 1, timeBuf, (rank >= 0) ? " *" : "");

        uint16_t col;
        if (f.scratched) {
            col = COL_RED;
        } else if (rank == 0) {
            col = COL_GREEN;   // 1st best
        } else if (rank == 1) {
            col = COL_ORANGE;  // 2nd best
        } else if (rank == 2) {
            col = COL_YELLOW;  // 3rd best
        } else {
            col = COL_WHITE;
        }

#ifdef WOKWI_SIM
        _drawCentered(row, DISPLAY_CX, y, col, 1);
        if (f.scratched) {
            // Draw strikethrough line
            int16_t x1, y1; uint16_t w, h;
            _tft.getTextBounds(row, 0, 0, &x1, &y1, &w, &h);
            int lineY = y;
            _tft.drawFastHLine(DISPLAY_CX - w/2 - 2, lineY, w + 4, COL_RED);
        }
#else
        _drawFontCentered(row, WS_CX, y, col, &FreeMonoBold18pt7b);
        if (f.scratched) {
            // Draw strikethrough line
            int16_t x1, y1; uint16_t w, h;
            _gfx->setFont(&FreeMonoBold18pt7b);
            _gfx->setTextSize(1);
            _gfx->getTextBounds(row, 0, 0, &x1, &y1, &w, &h);
            int lineY = y;
            _gfx->fillRect(WS_CX - w/2 - 4, lineY - 1, w + 8, 3, COL_RED);
            _gfx->setFont(nullptr);
        }
#endif
        shown++;
    }
}

// ── OTA firmware update screen ────────────────────────────────────────────────

void UI::renderOtaCheck(OtaStatus status, int progress, const char* availVer) {
    _clearScreen();
    _drawOtaCheck(status, progress, availVer);
    // The OTA screen is now what is physically on the panel — record that, or
    // leaving it for IDLE reads as "no change" and the firmware screen stays up.
    _invalidateCache(STATE_OTA_CHECK);
#ifndef WOKWI_SIM
    _gfx->flush();
#endif
}

// Every screen draws incrementally off a cache of what it last put on the panel,
// and render() decides what to redraw purely by comparing against that cache. So
// any code that rewrites the panel behind render()'s back has to say so here, or
// the next render takes a "nothing changed" branch and leaves the old picture up.
//
// This has bitten three times, all the same shape: blank() wiping the screen
// (I-37), and renderOtaCheck()/renderHistory() drawing a whole different screen
// without render() ever being involved.
//
// main.cpp's _lastState reset (see _wakeScreen) is not a substitute — that one
// only decides whether render() is *called*. This is the copy that decides what
// render() draws once it is.
void UI::_invalidateCache(AppState nowShowing) {
    _prevState       = nowShowing;
    _prevConnState   = (BaseConnState)255;
    _prevBatteryPct  = -1;
    _prevWtSecs      = -1;
    _prevFlashSecs   = -1;
    _prevAltFlightNo = -1;
    _prevPrepDs      = -1;
    _arcVisible      = true;
}

void UI::blank() {
    _clearScreen();
    // Nothing is on the panel at all, so no state can match: 255 forces the next
    // render down its full-redraw path whatever screen it is for.
    _invalidateCache((AppState)255);
#ifndef WOKWI_SIM
    _gfx->flush();
#endif
}

void UI::_drawOtaCheck(OtaStatus status, int progress, const char* availVer) {
#ifndef WOKWI_SIM
    ws_fillRing(_gfx, WS_CX, WS_CY, 225, 218, COL_DIMGRAY);

    _drawFontCentered("FIRMWARE", WS_CX, 55, COL_GRAY, &FreeSansBold18pt7b);

    _drawFontCentered(FW_VERSION, WS_CX, WS_CY - 20, COL_WHITE, &FreeSansBold24pt7b);

    // Status text (color-coded by state)
    char statusBuf[40] = "";
    uint16_t statusCol = COL_WHITE;
    switch (status) {
        case OTA_IDLE:                                                           break;
        case OTA_CHECKING:    strcpy(statusBuf, "CHECKING...");
                              statusCol = COL_GRAY;                              break;
        case OTA_UP_TO_DATE:  strcpy(statusBuf, "UP TO DATE");
                              statusCol = COL_GREEN;                             break;
        case OTA_AVAILABLE:
            snprintf(statusBuf, sizeof(statusBuf), "%s AVAIL",
                     (availVer && availVer[0]) ? availVer : "UPDATE");
            statusCol = COL_YELLOW;
            break;
        case OTA_DOWNLOADING:
            snprintf(statusBuf, sizeof(statusBuf), "LOADING  %d%%", progress);
            statusCol = COL_ORANGE;
            break;
        case OTA_SUCCESS:     strcpy(statusBuf, "DONE  REBOOTING...");
                              statusCol = COL_GREEN;                             break;
        case OTA_FAILED:      strcpy(statusBuf, "FAILED");
                              statusCol = COL_RED;                               break;
        case OTA_NO_WIFI:     strcpy(statusBuf, "NO WIFI");
                              statusCol = COL_RED;                               break;
        // Not an error for this timer — the base station is behind. Say whose
        // fault it is, or the CD stands there wondering why it will not update.
        case OTA_BASE_OLDER:  strcpy(statusBuf, "BASE IS OLDER");
                              statusCol = COL_ORANGE;                            break;
    }
    if (statusBuf[0]) {
        _drawFontCentered(statusBuf, WS_CX, WS_CY + 50, statusCol, &FreeSans12pt7b);
    }

    // Action hints (only shown when not mid-download)
    bool busy = (status == OTA_DOWNLOADING || status == OTA_SUCCESS);
    if (!busy) {
        if (status == OTA_AVAILABLE) {
            _drawFontCentered("HOLD R = UPDATE", WS_CX, 370, COL_WHITE, &FreeSans12pt7b);
        } else if (status == OTA_BASE_OLDER) {
            // Nothing this timer can do — point at the thing that needs fixing.
            _drawFontCentered("UPDATE THE BASE", WS_CX, 370, COL_ORANGE, &FreeSans12pt7b);
        } else if (status == OTA_FAILED || status == OTA_NO_WIFI) {
            // A dead end otherwise: without this the only way to re-run a check
            // is to exit and walk the four settings holds again.
            _drawFontCentered("L = RETRY", WS_CX, 370, COL_WHITE, &FreeSans12pt7b);
        }
        _drawFontCentered("R = EXIT", WS_CX, 408, COL_DIMGRAY, &FreeSans9pt7b);
    }
#else
    _drawCentered("OTA: HW ONLY", DISPLAY_CX, DISPLAY_CY, COL_GRAY, 1);
#endif
}

// ── Round history (NVS) ───────────────────────────────────────────────────────

void UI::renderHistory(int slot, const HistRound& hist, int totalSlots) {
    _clearScreen();
    _drawHistory(slot, hist, totalSlots);
    // Same as renderOtaCheck: round recall exits to IDLE or on to the OTA screen,
    // and both transitions need render() to see a state change.
    _invalidateCache(STATE_HISTORY);
#ifndef WOKWI_SIM
    _gfx->flush();
#endif
}

void UI::_drawHistory(int slot, const HistRound& hist, int totalSlots) {
#ifndef WOKWI_SIM
    ws_fillRing(_gfx, WS_CX, WS_CY, 225, 218, COL_DIMGRAY);

    // Title + slot indicator
    _drawFontCentered("ROUND RECALL", WS_CX, 55, COL_WHITE, &FreeSansBold18pt7b);
    char slotBuf[12];
    snprintf(slotBuf, sizeof(slotBuf), "%d of %d", slot + 1, totalSlots);
    _drawFontCentered(slotBuf, WS_CX, 92, COL_GRAY, &FreeSans9pt7b);

    if (!hist.valid || hist.count == 0) {
        _drawFontCentered("No data saved", WS_CX, 240, COL_DIMGRAY, &FreeSans12pt7b);
    } else {
        bool isF5K = (strncmp(hist.discipline, "F5K", 3) == 0);

        // Pilot name (if recorded)
        int disciplineY = 128;
        if (hist.pilotName[0] != '\0') {
            _drawFontCentered(hist.pilotName, WS_CX, 120, COL_GRAY, &FreeSans12pt7b);
            disciplineY = 152;
        }
        _drawFontCentered(hist.discipline, WS_CX, disciplineY,
                          isF5K ? COL_FUCHSIA : COL_ORANGE, &FreeSansBold18pt7b);

        const int step    = 36;
        const int maxShow = 7;
        const int startY  = (hist.pilotName[0] != '\0') ? 192 : 178;
        for (int i = 0; i < hist.count && i < maxShow; i++) {
            char timeBuf[16]; fmtMs(hist.flightMs[i], timeBuf, sizeof(timeBuf));
            char row[32];
            if (isF5K && hist.altitudeM[i] > 0) {
                snprintf(row, sizeof(row), "%d. %s  %03dm",
                         i + 1, timeBuf, (int)hist.altitudeM[i]);
            } else {
                snprintf(row, sizeof(row), "%d. %s", i + 1, timeBuf);
            }
            _drawFontCentered(row, WS_CX, startY + i * step,
                              COL_WHITE, &FreeMonoBold18pt7b);
        }
        if (hist.count > maxShow) {
            char more[16];
            snprintf(more, sizeof(more), "+%d more", hist.count - maxShow);
            _drawFontCentered(more, WS_CX, startY + maxShow * step,
                              COL_DIMGRAY, &FreeSans9pt7b);
        }
    }

    // Nav hint — slot 0 is newest so R exits there
    const char* hint = (slot == 0) ? "L=OLDER  R=EXIT  hold=EXIT"
                                   : "L=OLDER  R=NEWER  hold=EXIT";
    _drawFontCentered(hint, WS_CX, 448, COL_GRAY, &FreeSans9pt7b);
#endif
}
