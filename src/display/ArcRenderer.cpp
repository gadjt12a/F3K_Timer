#include "ArcRenderer.h"

#ifdef WOKWI_SIM
#include <math.h>

namespace ArcRenderer {

static const uint16_t COL_ARC_GREEN  = 0x07E0;
static const uint16_t COL_ARC_ORANGE = 0xFC40;
static const uint16_t COL_ARC_RED    = 0xF800;
static const uint16_t COL_BLACK      = 0x0000;

static uint16_t _arcColor(int remaining) {
    if (remaining > ARC_GREEN_THRESHOLD)  return COL_ARC_GREEN;
    if (remaining > ARC_ORANGE_THRESHOLD) return COL_ARC_ORANGE;
    return COL_ARC_RED;
}

static void _drawSlice(Adafruit_GFX& c, float startDeg, float endDeg, uint16_t col) {
    for (float a = startDeg; a < endDeg; a += 0.5f) {
        float rad = a * (float)M_PI / 180.0f;
        float s = sinf(rad);
        float cv = cosf(rad);
        c.drawLine(
            DISPLAY_CX + (int)(ARC_INNER_RADIUS * s),
            DISPLAY_CY - (int)(ARC_INNER_RADIUS * cv),
            DISPLAY_CX + (int)(ARC_OUTER_RADIUS * s),
            DISPLAY_CY - (int)(ARC_OUTER_RADIUS * cv),
            col);
    }
}

void draw(Adafruit_GFX& canvas, int remaining, int total) {
    if (total <= 0 || remaining <= 0) return;
    if (remaining <= ARC_RED_THRESHOLD) {
        if ((millis() % (ARC_FLASH_INTERVAL_MS * 2)) >= ARC_FLASH_INTERVAL_MS) return;
    }
    _drawSlice(canvas, 0.0f, (float)remaining / total * 360.0f, _arcColor(remaining));
}

void erase(Adafruit_GFX& canvas, int remaining, int total) {
    if (total <= 0 || remaining <= 0) return;
    _drawSlice(canvas, 0.0f, (float)remaining / total * 360.0f, COL_BLACK);
}

void eraseConsumed(Adafruit_GFX& canvas, int prevRemaining, int newRemaining, int total) {
    if (total <= 0 || prevRemaining <= newRemaining) return;
    float startDeg = (float)newRemaining / total * 360.0f;
    float endDeg   = (float)prevRemaining / total * 360.0f;
    _drawSlice(canvas, startDeg, endDeg + 0.5f, COL_BLACK);
}

} // namespace ArcRenderer

#endif // WOKWI_SIM
