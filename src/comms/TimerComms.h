#pragma once
#include "config.h"

enum CommsState : uint8_t {
    COMMS_IDLE,         // begin() not yet called
    COMMS_CONNECTING,   // WiFi or TCP connecting (5-min budget)
    COMMS_CONNECTED,    // WiFi + TCP up, protocol active
    COMMS_FAILED        // gave up — reboot to retry
};

class TimerComms {
public:
    void begin();
    void update();      // call every loop — non-blocking

    bool          isConnected()   const { return _state == COMMS_CONNECTED; }
    CommsState    getState()      const { return _state; }
    BaseConnState baseConnState() const;

    // Pending commands — read-and-clear: returns true once, then false until next event
    bool hasStartCommand();     // base sent START
    bool hasStopCommand();      // base sent STOP
    bool hasTaskUpdate();       // base sent TASK — read getTaskWtSeconds() before next update
    bool hasPilotList();        // base sent PILOTS
    bool hasCountdown();        // base sent COUNT N — read getCountdownN() before next call

    int  getTaskWtSeconds() const { return _taskWtSeconds; }
    int  getTimerId()       const { return _timerId; }
    int  getCountdownN()    const { return _countdownN; }

    int          getPilotCount()    const { return _pilotCount; }
    const Pilot& getPilot(int idx)  const { return _pilots[idx]; }

    void sendFlight(int pilotId, unsigned long durationMs);

private:
    CommsState _state         = COMMS_IDLE;
    int        _timerId       = -1;
    int        _taskWtSeconds = 600;
    int        _pilotCount    = 0;
    Pilot      _pilots[MAX_PILOTS];

    bool _hasStartCommand = false;
    bool _hasStopCommand  = false;
    bool _hasTaskUpdate   = false;
    bool _hasPilotList    = false;
    bool _hasCountdown    = false;
    int  _countdownN      = 0;

    unsigned long _budgetStartMs    = 0;  // start of current 5-min connect window
    unsigned long _connectStartMs   = 0;  // start of current WiFi attempt (60s each)
    unsigned long _lastPingMs       = 0;
    unsigned long _lastRxMs         = 0;
    unsigned long _lastTcpAttemptMs = 0;

    static const unsigned long CONNECT_BUDGET_MS     = 300000; // 5 min total before giving up
    static const unsigned long WIFI_ATTEMPT_MS       = 60000;  // restart WiFi every 60s within budget
    static const unsigned long TCP_RETRY_INTERVAL_MS = 5000;
    static const unsigned long PING_INTERVAL_MS      = 30000;
    static const unsigned long RX_TIMEOUT_MS         = 90000;  // no PONG in 90s = dead socket

    static const int RX_BUF_SIZE = 256;
    char _rxBuf[RX_BUF_SIZE];
    int  _rxLen = 0;

#ifndef WOKWI_SIM
    void _readLines();
    void _parseLine(const char* line);
    void _parsePilots(const char* data);
    void _sendLine(const char* line);
#endif
};
