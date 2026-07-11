#include "RoundHistory.h"
#include <string.h>

#ifndef WOKWI_SIM
#include <Arduino.h>
#include <Preferences.h>
static Preferences _prefs;
static bool _open = false;
static void _openPrefs() { if (!_open) { _prefs.begin("f3k", false); _open = true; } }
#endif

// NVS key builders — slot 0/1, index 0-9 → keys like "r0v", "r0f3", "r1a9" (all ≤ 4 chars)
static void _kv(int s, char* b) { snprintf(b, 8, "r%dv",  s); }
static void _kd(int s, char* b) { snprintf(b, 8, "r%dd",  s); }
static void _kn(int s, char* b) { snprintf(b, 8, "r%dn",  s); }
static void _kf(int s, int i, char* b) { snprintf(b, 8, "r%df%d", s, i); }
static void _ka(int s, int i, char* b) { snprintf(b, 8, "r%da%d", s, i); }

// ─────────────────────────────────────────────────────────────────────────────

void RoundHistory::begin() {
#ifndef WOKWI_SIM
    _openPrefs();
#endif
    memset(&_current, 0, sizeof(_current));
    _loadSlot(0, _current);
}

void RoundHistory::startRound(bool isF5K) {
    // Shift slot 0 → slot 1, then start fresh in slot 0
    HistRound prev;
    _loadSlot(0, prev);
    _saveSlot(1, prev);
    memset(&_current, 0, sizeof(_current));
    _current.valid = true;
    strncpy(_current.discipline, isF5K ? "F5K" : "F3K", 4);
    _saveSlot(0, _current);
}

void RoundHistory::recordFlight(unsigned long durationMs) {
    if (_current.count >= HIST_MAX_FLIGHTS) return;
    int i = _current.count++;
    _current.flightMs[i]  = (uint32_t)durationMs;
    _current.altitudeM[i] = 0;
#ifndef WOKWI_SIM
    char key[8];
    _kn(0, key); _prefs.putUChar(key, _current.count);
    _kf(0, i, key); _prefs.putUInt(key, (uint32_t)durationMs);
#endif
}

void RoundHistory::recordAltitude(int flightNo, int altM) {
    int idx = flightNo - 1;
    if (idx < 0 || idx >= _current.count) return;
    _current.altitudeM[idx] = (int16_t)altM;
#ifndef WOKWI_SIM
    char key[8];
    _ka(0, idx, key); _prefs.putShort(key, (int16_t)altM);
#endif
}

bool RoundHistory::load(int slot, HistRound& out) {
    _loadSlot(slot, out);
    return out.valid;
}

void RoundHistory::_saveSlot(int slot, const HistRound& r) {
#ifndef WOKWI_SIM
    char key[8];
    _kv(slot, key); _prefs.putBool(key, r.valid);
    _kd(slot, key); _prefs.putString(key, r.discipline);
    _kn(slot, key); _prefs.putUChar(key, r.count);
    for (int i = 0; i < r.count && i < HIST_MAX_FLIGHTS; i++) {
        _kf(slot, i, key); _prefs.putUInt(key,  r.flightMs[i]);
        _ka(slot, i, key); _prefs.putShort(key, r.altitudeM[i]);
    }
#else
    (void)slot; (void)r;
#endif
}

void RoundHistory::_loadSlot(int slot, HistRound& out) {
    memset(&out, 0, sizeof(out));
#ifndef WOKWI_SIM
    char key[8];
    _kv(slot, key); out.valid = _prefs.getBool(key, false);
    if (!out.valid) return;
    _kd(slot, key);
    String d = _prefs.getString(key, "F3K");
    strncpy(out.discipline, d.c_str(), 3); out.discipline[3] = '\0';
    _kn(slot, key); out.count = _prefs.getUChar(key, 0);
    for (int i = 0; i < out.count && i < HIST_MAX_FLIGHTS; i++) {
        _kf(slot, i, key); out.flightMs[i]  = _prefs.getUInt(key,  0);
        _ka(slot, i, key); out.altitudeM[i] = _prefs.getShort(key, 0);
    }
#else
    (void)slot;
#endif
}
