#pragma once
#include "config.h"

enum CommsState : uint8_t {
    COMMS_IDLE,         // begin() not yet called
    COMMS_CONNECTING,   // WiFi or TCP connecting (5-min budget)
    COMMS_CONNECTED,    // WiFi + TCP up, protocol active
    COMMS_FAILED        // unreachable since session 31 (retry is now endless); kept for ABI
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
    bool hasPrepStart();        // base sent PREP t=N — read getPrepSeconds() before next call
    bool hasLandStart();        // base sent LAND t=N — read getLandSeconds() before next call
    bool hasScreenCmd();        // base sent SCREEN t=N — read getScreenSeconds() before next call

    int  getTaskWtSeconds() const { return _taskWtSeconds; }
    int  getTimerId()       const { return _timerId; }
    int  getCountdownN()    const { return _countdownN; }
    int  getPrepSeconds()   const { return _prepSeconds; }
    int  getLandSeconds()   const { return _landSeconds; }
    int  getScreenSeconds() const { return _screenSeconds; }
    bool isF5K()            const { return _isF5K; }

    int          getPilotCount()    const { return _pilotCount; }
    const Pilot& getPilot(int idx)  const { return _pilots[idx]; }

    void sendFlight(int pilotId, unsigned long durationMs);
    void sendJumped(int pilotId, unsigned long durationMs);  // jumped start — CD note only
    void sendAltitude(int pilotId, int flightNo, int altM);
    void sendSelect(int pilotId);

private:
    CommsState _state         = COMMS_IDLE;
    int        _timerId       = -1;
    int        _taskWtSeconds = 600;
    bool       _isF5K         = false;
    int        _pilotCount    = 0;
    Pilot      _pilots[MAX_PILOTS];

    bool _hasStartCommand = false;
    bool _hasStopCommand  = false;
    bool _hasTaskUpdate   = false;
    bool _hasPilotList    = false;
    bool _hasCountdown    = false;
    bool _hasPrepStart    = false;
    bool _hasLandStart    = false;
    bool _hasScreenCmd    = false;
    int  _countdownN      = 0;
    int  _prepSeconds     = 0;
    int  _landSeconds     = 0;
    int  _screenSeconds   = 0;

    unsigned long _budgetStartMs       = 0;  // start of current 5-min connect window
    unsigned long _connectStartMs      = 0;  // start of current WiFi attempt (60s each)
    unsigned long _lastPingMs          = 0;
    unsigned long _lastRxMs            = 0;
    unsigned long _lastTcpAttemptMs    = 0;
    unsigned long _lastWifiStatusLogMs = 0;
    unsigned long _lastSleepAssertMs   = 0;  // last time WiFi.setSleep(false) was re-applied

    static const unsigned long CONNECT_BUDGET_MS     = 300000; // 5 min total before giving up
    static const unsigned long WIFI_ATTEMPT_MS       = 30000;  // restart WiFi every 30s within budget
    static const unsigned long TCP_RETRY_INTERVAL_MS = 5000;
    static const unsigned long PING_INTERVAL_MS      = 30000;
    static const unsigned long RX_TIMEOUT_MS         = 45000;  // one missed 30s PONG + 15s grace = dead socket

    static const int RX_BUF_SIZE = 256;
    char _rxBuf[RX_BUF_SIZE];
    int  _rxLen = 0;

    // Outbound message buffer for FLIGHT/JUMPED/ALTITUDE/SELECT.
    //
    // An entry stays here until the base ACKs it byte-for-byte. Sending is NOT
    // proof of delivery: on a silently dead socket (AP glitch, no FIN/RST) lwIP
    // accepts the write and discards it, and _tcp.connected() keeps returning
    // true for up to ~60 s. The old code dequeued at send time, so a flight time
    // written into that hole was simply gone.
    //
    // A plain array rather than the previous ring: ACKs let entries leave from
    // the middle, which a head/tail ring cannot express.
    static const int PENDING_MAX  = 16;
    static const int PENDING_LINE = 64;
    struct PendingMsg {
        char          line[PENDING_LINE];
        unsigned long lastSentMs;   // 0 = queued but never yet put on the wire
        uint16_t      attempts;
    };
    PendingMsg _pending[PENDING_MAX];
    int        _pendingCount = 0;

    // Retry cadence for un-ACKed messages while connected. A healthy round trip
    // is well under 100 ms, so 5 s only fires when something is genuinely wrong,
    // and the base dedups replayed FLIGHTs on (pilot, group, duration).
    static const unsigned long ACK_RETRY_MS = 5000;

#ifndef WOKWI_SIM
    void _readLines();
    void _parseLine(const char* line);
    void _parsePilots(const char* data);
    void _sendLine(const char* line);
    void _sendOrQueue(const char* line);   // queue, then send if the socket looks alive
    void _enqueue(const char* line);
    void _flushPending();                  // (re)send everything still awaiting an ACK
    void _ackPending(const char* msg);     // drop the entry the base just confirmed
    void _retryPending();                  // resend anything un-ACKed past ACK_RETRY_MS
#endif
};
