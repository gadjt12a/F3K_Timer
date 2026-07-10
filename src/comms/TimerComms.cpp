#include "TimerComms.h"
#include <string.h>
#include <stdlib.h>

#ifndef WOKWI_SIM
#include <WiFi.h>
#include <WiFiClient.h>

static WiFiClient _tcp;
#endif

// ── Public API ────────────────────────────────────────────────────────────────

void TimerComms::begin() {
#ifndef WOKWI_SIM
    Serial.println("[COMMS] Starting WiFi connect to " WIFI_SSID);
    WiFi.mode(WIFI_STA);
    // Disable WiFi modem sleep. With sleep on (the default), the radio dozes during
    // quiet periods (e.g. the prep countdown) and drops the TCP link ~1 min in, forcing
    // a reconnect that loses the selected pilot mid-round. Keep the radio awake.
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long now = millis();
    _budgetStartMs  = now;
    _connectStartMs = now;
    _state = COMMS_CONNECTING;
#endif
}

void TimerComms::update() {
#ifndef WOKWI_SIM
    unsigned long now = millis();

    switch (_state) {
        case COMMS_IDLE:
            break;

        case COMMS_CONNECTING:
            // Give up after 5-minute total budget
            if (now - _budgetStartMs > CONNECT_BUDGET_MS) {
                Serial.println("[COMMS] 5-minute budget exhausted — standalone mode (reboot to retry)");
                _tcp.stop();
                WiFi.disconnect();
                _state = COMMS_FAILED;
                break;
            }
            // Restart WiFi every 60s if still not associated
            if (WiFi.status() != WL_CONNECTED && now - _connectStartMs > WIFI_ATTEMPT_MS) {
                Serial.printf("[COMMS] WiFi attempt timeout — retrying (%lus budget remaining)\n",
                              (CONNECT_BUDGET_MS - (now - _budgetStartMs)) / 1000);
                _lastTcpAttemptMs = 0;
                WiFi.disconnect();
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                WiFi.setSleep(false);
                _connectStartMs = now;
                break;
            }
            // WiFi up — attempt TCP every 5s
            if (WiFi.status() == WL_CONNECTED) {
                if (now - _lastTcpAttemptMs >= TCP_RETRY_INTERVAL_MS) {
                    _lastTcpAttemptMs = now;
                    _tcp.stop();
                    Serial.printf("[COMMS] TCP connect attempt → %s:%d\n", BASE_HOST, BASE_PORT);
                    if (_tcp.connect(BASE_HOST, BASE_PORT)) {
                        Serial.printf("[COMMS] Connected (IP %s)\n", WiFi.localIP().toString().c_str());
                        _state = COMMS_CONNECTED;
                        _lastPingMs = now;
                        _lastRxMs   = now;
                        char buf[40];
                        snprintf(buf, sizeof(buf), "JOIN mac=%s", WiFi.macAddress().c_str());
                        _sendLine(buf);
                    }
                }
            }
            break;

        case COMMS_CONNECTED:
            if (!_tcp.connected() || (now - _lastRxMs > RX_TIMEOUT_MS)) {
                Serial.printf("[COMMS] TCP dropped: connected=%d rxAge=%lus\n",
                              (int)_tcp.connected(), (now - _lastRxMs) / 1000);
                _tcp.stop();
                _budgetStartMs  = now;  // fresh 5-minute budget for reconnect
                _connectStartMs = now;
                _lastTcpAttemptMs = 0;
                _state = COMMS_CONNECTING;
                break;
            }
            _readLines();
            if (now - _lastPingMs > PING_INTERVAL_MS) {
                Serial.println("[COMMS] TX: PING");
                _sendLine("PING");
                _lastPingMs = now;
            }
            break;

        case COMMS_FAILED:
            break;  // terminal — reboot to retry
    }
#endif
}

BaseConnState TimerComms::baseConnState() const {
    switch (_state) {
        case COMMS_CONNECTED:  return BASE_CONNECTED;
        case COMMS_CONNECTING: return BASE_CONNECTING;
        default:               return BASE_DISCONNECTED;
    }
}

bool TimerComms::hasStartCommand()  { bool v = _hasStartCommand;  _hasStartCommand  = false; return v; }
bool TimerComms::hasStopCommand()   { bool v = _hasStopCommand;   _hasStopCommand   = false; return v; }
bool TimerComms::hasTaskUpdate()    { bool v = _hasTaskUpdate;    _hasTaskUpdate    = false; return v; }
bool TimerComms::hasPilotList()     { bool v = _hasPilotList;     _hasPilotList     = false; return v; }
bool TimerComms::hasCountdown()     { bool v = _hasCountdown;     _hasCountdown     = false; return v; }

void TimerComms::sendFlight(int pilotId, unsigned long durationMs) {
#ifndef WOKWI_SIM
    if (_state != COMMS_CONNECTED) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "FLIGHT pilot=%d dur=%lu", pilotId, durationMs);
    _sendLine(buf);
#endif
}

void TimerComms::sendAltitude(int pilotId, int flightNo, int altM) {
#ifndef WOKWI_SIM
    if (_state != COMMS_CONNECTED) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "ALTITUDE pilot=%d flight=%d alt=%d", pilotId, flightNo, altM);
    _sendLine(buf);
#endif
}

// ── Private — WiFi/TCP ────────────────────────────────────────────────────────

#ifndef WOKWI_SIM

void TimerComms::_sendLine(const char* line) {
    _tcp.print(line);
    _tcp.print('\n');
    _tcp.flush();
}

void TimerComms::_readLines() {
    while (_tcp.available()) {
        char c = (char)_tcp.read();
        if (c == '\n' || c == '\r') {
            if (_rxLen > 0) {
                _rxBuf[_rxLen] = '\0';
                _parseLine(_rxBuf);
                _rxLen = 0;
            }
        } else if (_rxLen < RX_BUF_SIZE - 1) {
            _rxBuf[_rxLen++] = c;
        }
    }
}

void TimerComms::_parseLine(const char* line) {
    _lastRxMs = millis();
    Serial.printf("[COMMS] RX: %s\n", line);

    if (strncmp(line, "ASSIGN id=", 10) == 0) {
        _timerId = atoi(line + 10);
        Serial.printf("[COMMS] Assigned timer ID: %d\n", _timerId);

    } else if (strncmp(line, "TASK wt=", 8) == 0) {
        _taskWtSeconds = atoi(line + 8);
        const char* disc = strstr(line, "disc=");
        _isF5K = disc && strncmp(disc + 5, "F5K", 3) == 0;
        _hasTaskUpdate = true;
        Serial.printf("[COMMS] Task update: WT=%ds disc=%s\n", _taskWtSeconds, _isF5K ? "F5K" : "F3K");

    } else if (strcmp(line, "START") == 0) {
        _hasStartCommand = true;
        Serial.println("[COMMS] START command received");

    } else if (strcmp(line, "STOP") == 0) {
        _hasStopCommand = true;
        Serial.println("[COMMS] STOP command received");

    } else if (strncmp(line, "PILOTS ", 7) == 0) {
        _parsePilots(line + 7);

    } else if (strncmp(line, "COUNT ", 6) == 0) {
        _countdownN  = atoi(line + 6);
        _hasCountdown = true;
        Serial.printf("[COMMS] Countdown: %d\n", _countdownN);

    } else if (strcmp(line, "PONG") == 0) {
        // keepalive — _lastRxMs already updated above

    } else {
        Serial.printf("[COMMS] Unknown message: %s\n", line);
    }
}

// Parse "1:Alice Smi,2:Bob Jon,3:Charlie Bro"
void TimerComms::_parsePilots(const char* data) {
    _pilotCount = 0;

    char buf[256];
    strncpy(buf, data, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* token = strtok(buf, ",");
    while (token && _pilotCount < MAX_PILOTS) {
        char* colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            _pilots[_pilotCount].id = atoi(token);
            strncpy(_pilots[_pilotCount].name, colon + 1, MAX_PILOT_NAME);
            _pilots[_pilotCount].name[MAX_PILOT_NAME] = '\0';
            _pilotCount++;
        }
        token = strtok(nullptr, ",");
    }

    _hasPilotList = true;
    Serial.printf("[COMMS] Received %d pilots\n", _pilotCount);
}

#endif  // WOKWI_SIM
