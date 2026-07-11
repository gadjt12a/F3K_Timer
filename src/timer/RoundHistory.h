#pragma once
#include <stdint.h>
#include "config.h"

#define HIST_MAX_FLIGHTS MAX_FLIGHTS   // 10 — same cap as live FlightLog

struct HistRound {
    bool     valid;
    char     discipline[4];            // "F3K" or "F5K"
    uint8_t  count;
    uint32_t flightMs[HIST_MAX_FLIGHTS];
    int16_t  altitudeM[HIST_MAX_FLIGHTS];  // 0 = not recorded
};

// Stores last 2 complete rounds in ESP32 NVS (Preferences namespace "f3k").
// Slot 0 = most recently started round (in-progress or last complete).
// Slot 1 = the round before that.
// Writes are incremental — each flight is persisted immediately so no data
// is lost if the device loses power mid-round.
class RoundHistory {
public:
    void begin();
    void startRound(bool isF5K);               // call at start of every new round
    void recordFlight(unsigned long durationMs);
    void recordAltitude(int flightNo, int altM); // flightNo is 1-based
    bool load(int slot, HistRound& out);       // 0=current/recent, 1=previous

private:
    HistRound _current;   // in-RAM mirror of slot 0
    void _saveSlot(int slot, const HistRound& r);
    void _loadSlot(int slot, HistRound& out);
};
