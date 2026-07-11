#include "Buttons.h"
#include "pin_config.h"

void Buttons::begin() {
#ifdef WOKWI_SIM
    pinMode(BTN_A_SIM, INPUT_PULLUP);
    pinMode(BTN_B_SIM, INPUT_PULLUP);
    Serial.println("[BTN] Wokwi GPIO init");
#else
    if (!_pmu.begin(Wire, ADDR_AXP2101, IIC_SDA, IIC_SCL)) {
        Serial.println("[BTN] AXP2101 init FAILED");
    } else {
        // Prevent sleep mode from suppressing PKEY events.
        _pmu.disableSleep();

        _pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        // Enable all four PKEY IRQ types. In update() we act only on POSITIVE
        // (release) so that holding the button to power off does not trigger
        // a click — the AXP2101 powers the device off before the release IRQ fires.
        _pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ    |
                       XPOWERS_AXP2101_PKEY_LONG_IRQ      |
                       XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ  |
                       XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
        _pmu.clearIrqStatus();

        // Enable power rails for audio subsystem
        _pmu.setALDO2Voltage(3300); _pmu.enableALDO2();
        _pmu.setALDO3Voltage(3300); _pmu.enableALDO3();
        _pmu.setALDO4Voltage(3300); _pmu.enableALDO4();
        _pmu.setBLDO1Voltage(3300); _pmu.enableBLDO1();
        _pmu.setBLDO2Voltage(3300); _pmu.enableBLDO2();

        Serial.println("[BTN] AXP2101 ready");
    }
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
    // On the very first update() call, flush any PKEY IRQs that accumulated
    // during setup() (power-on button release, spurious events) and open a
    // 300ms window for stragglers. The window starts here — not in begin() —
    // because setup() can take 2-3s, which would expire a begin()-time window
    // before the loop even starts.
    if (!_firstUpdateDone) {
        _firstUpdateDone = true;
        if (_pmu.getIrqStatus() & 0x0F00) {
            _pmu.clearIrqStatus();
            _startupIgnoreUntilMs = millis() + 300;
            Serial.println("[BTN] boot-time PKEY events flushed");
        }
    }

    // Read INTSTS2 directly from getIrqStatus() return bits[15:8].
    // Clear on any PKEY event (0x0F00) but only register a click on POSITIVE
    // (0x0100 = _BV(8) = release edge), so holding L to power off never
    // triggers a click — the device powers down before the release fires.
    bool rawA = false;
    uint32_t irqStatus = _pmu.getIrqStatus();
    if (irqStatus & 0x0F00) {
        _pmu.clearIrqStatus();
        if (irqStatus & 0x0100) {
            if (millis() < _startupIgnoreUntilMs) {
                Serial.println("[BTN] A (PWR) startup click ignored");
            } else {
                rawA = true;
            }
        }
    }
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
    // 200ms cooldown guards against bouncy POSITIVE edges from a single release.
    if (rawA && (now - _lastAClickMs >= 200)) {
        _clickA      = true;
        _lastAClickMs = now;
        Serial.println("[BTN] A (PWR) clicked");
    }
#endif

    // ── Button B (with debounce) ──────────────────────────────────────────────
    _holdB  = false;
    _clickB = false;
    _veryLongB = false;

    bool stableB = _prevB;
    if (rawB != _prevB) {
        if (now - _lastBChangeMs >= DEBOUNCE_MS) {
            stableB = rawB;
            _lastBChangeMs = now;
        }
    }

    if (stableB && !_prevB) {
        _pressedBms = now;
        _holdFiredB = false;
        _veryLongFiredB = false;
    } else if (stableB && _prevB) {
        if (!_holdFiredB && (now - _pressedBms >= LONG_PRESS_MS)) {
            _holdB      = true;
            _holdFiredB = true;
        }
        if (!_veryLongFiredB && (now - _pressedBms >= VERY_LONG_PRESS_MS)) {
            _veryLongB      = true;
            _veryLongFiredB = true;
        }
    } else if (!stableB && _prevB) {
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

int Buttons::getBatteryPercent() {
#ifdef WAVESHARE_HW
    return _pmu.getBatteryPercent();
#else
    return 75;
#endif
}

bool Buttons::isCharging() {
#ifdef WAVESHARE_HW
    return _pmu.isCharging();
#else
    return false;
#endif
}
