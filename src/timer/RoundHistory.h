#pragma once
#include <stdint.h>
#include "config.h"

#define HIST_MAX_FLIGHTS MAX_FLIGHTS   // 10 — same cap as live FlightLog

struct HistRound {
    bool     valid;
    char     discipline[4];                      // "F3K" or "F5K"
    char     pilotName[MAX_PILOT_NAME + 1];      // pilot name at start of round
    uint8_t  count;
    uint32_t flightMs[HIST_MAX_FLIGHTS];
    int16_t  altitudeM[HIST_MAX_FLIGHTS];        // 0 = not recorded
};

// Stores last HIST_SLOTS complete rounds in ESP32 NVS (Preferences namespace "f3k").
// Slot 0 = most recently started round (in-progress or last complete).
// Slot 1 = the round before that.  Slot 2 = oldest stored.
// Writes are incremental — each flight is persisted immediately so no data
// is lost if the device loses power mid-round.
class RoundHistory {
public:
    void begin();
    void startRound(bool isF5K, const char* pilotName = "");  // call at start of every new round
    void recordFlight(unsigned long durationMs);
    void recordAltitude(int flightNo, int altM);              // flightNo is 1-based
    bool load(int slot, HistRound& out);                      // 0=most recent .. HIST_SLOTS-1=oldest

private:
    HistRound _current;   // in-RAM mirror of slot 0
    void _saveSlot(int slot, const HistRound& r);
    void _loadSlot(int slot, HistRound& out);
};
