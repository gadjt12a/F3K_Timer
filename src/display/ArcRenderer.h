#pragma once
#include "config.h"

#ifdef WOKWI_SIM
#include <Adafruit_GFX.h>

namespace ArcRenderer {
    void draw(Adafruit_GFX& canvas, int remaining, int total);
    void erase(Adafruit_GFX& canvas, int remaining, int total);
    void eraseConsumed(Adafruit_GFX& canvas, int prevRemaining, int newRemaining, int total);
}
#endif
