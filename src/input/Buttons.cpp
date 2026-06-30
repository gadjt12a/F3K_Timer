#include "Buttons.h"
#include "pin_config.h"

void Buttons::begin() {
#ifdef WOKWI_SIM
    pinMode(BTN_A_SIM, INPUT_PULLUP);
    pinMode(BTN_B_SIM, INPUT_PULLUP);
    Serial.println("[BTN] Wokwi GPIO init");
#else
    // AXP2101 for Button A (PWR key) — also initializes Wire
    if (!_pmu.begin(Wire, ADDR_AXP2101, IIC_SDA, IIC_SCL)) {
        Serial.println("[BTN] AXP2101 init FAILED");
    } else {
        _pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        _pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
        _pmu.clearIrqStatus();

        // Enable ALL potentially relevant power rails for audio subsystem
        // Different Waveshare boards use different rails - enable all candidates
        _pmu.setALDO2Voltage(3300);
        _pmu.enableALDO2();
        Serial.println("[BTN] AXP2101 ALDO2 enabled (3.3V)");

        _pmu.setALDO3Voltage(3300);
        _pmu.enableALDO3();
        Serial.println("[BTN] AXP2101 ALDO3 enabled (3.3V)");

        _pmu.setALDO4Voltage(3300);
        _pmu.enableALDO4();
        Serial.println("[BTN] AXP2101 ALDO4 enabled (3.3V)");

        _pmu.setBLDO1Voltage(3300);
        _pmu.enableBLDO1();
        Serial.println("[BTN] AXP2101 BLDO1 enabled (3.3V)");

        _pmu.setBLDO2Voltage(3300);
        _pmu.enableBLDO2();
        Serial.println("[BTN] AXP2101 BLDO2 enabled (3.3V)");

        Serial.println("[BTN] AXP2101 power key ready");
    }
    // GPIO0 for Button B (BOOT button)
    pinMode(BTN_BOOT, INPUT_PULLUP);
    Serial.println("[BTN] GPIO0 BOOT button ready");
#endif
    _lastBChangeMs = millis();
}

void Buttons::update() {
    unsigned long now = millis();

#ifdef WOKWI_SIM
    bool rawA = (digitalRead(BTN_A_SIM) == LOW);
    bool rawB = (digitalRead(BTN_B_SIM) == LOW);
#else
    // Button A: AXP2101 power key short press (fires as event, not level)
    bool rawA = false;
    _pmu.getIrqStatus();
    if (_pmu.isPekeyShortPressIrq()) {
        rawA = true;
        _pmu.clearIrqStatus();
    }
    // Button B: GPIO0 (BOOT) — active LOW
    bool rawB = (digitalRead(BTN_BOOT) == LOW);
#endif

    // ── Button A ──────────────────────────────────────────────────────────────
    _holdA  = false;
    _clickA = false;

#ifdef WOKWI_SIM
    if (rawA && !_prevA) {
        _pressedAms = now;
        _holdFiredA = false;
        _clickA     = true;
    } else if (rawA && _prevA) {
        if (!_holdFiredA && (now - _pressedAms >= LONG_PRESS_MS)) {
            _holdA      = true;
            _holdFiredA = true;
        }
    } else if (!rawA) {
        _pressedAms = 0;
        _holdFiredA = false;
    }
    _prevA = rawA;
#else
    // AXP2101: IRQ fires once per short press — treat as click immediately
    // Long-press handled by AXP2101 hardware (power off)
    if (rawA) {
        _clickA = true;
        Serial.println("[BTN] A (PWR) clicked");
    }
#endif

    // ── Button B (with debounce) ──────────────────────────────────────────────
    _holdB  = false;
    _clickB = false;
    _veryLongB = false;

    // Debounce: only register state change after DEBOUNCE_MS
    bool stableB = _prevB;
    if (rawB != _prevB) {
        if (now - _lastBChangeMs >= DEBOUNCE_MS) {
            stableB = rawB;
            _lastBChangeMs = now;
        }
    }

    if (stableB && !_prevB) {
        // Falling edge — start timing
        _pressedBms = now;
        _holdFiredB = false;
        _veryLongFiredB = false;
    } else if (stableB && _prevB) {
        // Held — fire hold once at threshold, very long at 2s
        if (!_holdFiredB && (now - _pressedBms >= LONG_PRESS_MS)) {
            _holdB      = true;
            _holdFiredB = true;
        }
        if (!_veryLongFiredB && (now - _pressedBms >= VERY_LONG_PRESS_MS)) {
            _veryLongB      = true;
            _veryLongFiredB = true;
        }
    } else if (!stableB && _prevB) {
        // Rising edge — short press fires click only if hold never fired
        if (!_holdFiredB) {
            _clickB = true;
            Serial.println("[BTN] B (BOOT) clicked");
        }
        _pressedBms = 0;
        _holdFiredB = false;
        _veryLongFiredB = false;
    }
    _prevB = stableB;
}

bool Buttons::btnAClicked()  const { return _clickA; }
bool Buttons::btnBClicked()  const { return _clickB; }
bool Buttons::btnAHeld()     const { return _holdA; }
bool Buttons::btnBHeld()     const { return _holdB; }
bool Buttons::btnAVeryLong() const { return _veryLongA; }
bool Buttons::btnBVeryLong() const { return _veryLongB; }
