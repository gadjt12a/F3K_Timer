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
    // Full OFF→STA cycle clears any residual WiFi stack state left by a firmware
    // flash reset (RTS/EN toggle). Without this, the stack can get stuck in an
    // intermediate state and never scan for the AP.
    WiFi.persistent(false);  // RAM-only: skip stale NVS channel/BSSID cache
    WiFi.disconnect(true);   // tear down lwIP fully (clears any cached IP/DHCP state)
    WiFi.mode(WIFI_OFF);
    delay(500);              // 500ms: allow modem to fully power down before restart
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // force DHCP (don't use cached IP)
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
            // Budget expiry no longer terminal (session 31): field devices must
            // recover if the base station comes back after a long outage. Log and
            // start a fresh budget window, keep retrying forever.
            if (now - _budgetStartMs > CONNECT_BUDGET_MS) {
                Serial.println("[COMMS] 5-minute budget elapsed — restarting connect cycle");
                _budgetStartMs = now;
            }
            // Log WiFi status every 10s so we can see what the stack is doing
            if (WiFi.status() != WL_CONNECTED && now - _lastWifiStatusLogMs >= 10000) {
                Serial.printf("[COMMS] WiFi status=%d elapsed=%lus\n",
                              (int)WiFi.status(), (now - _connectStartMs) / 1000);
                _lastWifiStatusLogMs = now;
            }
            // Restart WiFi every 60s if still not associated
            if (WiFi.status() != WL_CONNECTED && now - _connectStartMs > WIFI_ATTEMPT_MS) {
                Serial.printf("[COMMS] WiFi attempt timeout — retrying (%lus budget remaining)\n",
                              (CONNECT_BUDGET_MS - (now - _budgetStartMs)) / 1000);
                _lastTcpAttemptMs = 0;
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                delay(500);
                WiFi.mode(WIFI_STA);
                WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
                WiFi.setSleep(false);
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
                        // Nagle off. Every message here is small, so with Nagle on
                        // lwIP holds a write whenever anything is still unACKed at
                        // the TCP level — which is precisely what a degraded link
                        // causes. Measured on the bench: with the base's ACKs
                        // blackholed, a FLIGHT and two application-level retries all
                        // sat in the send buffer and went out only when the link
                        // came back, so the 5 s retry was doing nothing on the wire.
                        // There is nothing to coalesce in a low-rate command
                        // protocol; delivery latency matters far more than packets.
                        _tcp.setNoDelay(true);
                        // Re-apply power save disable after WPA association. The IDF
                        // resets to WIFI_PS_MIN_MODEM during the handshake, so calling
                        // this only in begin() is not enough — re-apply once TCP is up.
                        WiFi.setSleep(false);
                        Serial.printf("[COMMS] Connected (IP %s)\n", WiFi.localIP().toString().c_str());
                        _state = COMMS_CONNECTED;
                        _lastPingMs = now;
                        _lastRxMs   = now;
                        // Report the running firmware so the CD can see, from the
                        // base station, which timers still need an update — there
                        // is otherwise no way to know without picking each one up.
                        // Older bases ignore unknown JOIN params, so this is safe
                        // to send unconditionally.
                        char buf[64];
                        snprintf(buf, sizeof(buf), "JOIN mac=%s fw=%s",
                                 WiFi.macAddress().c_str(), FW_VERSION);
                        _sendLine(buf);
                    }
                }
            }
            break;

        case COMMS_CONNECTED:
            // Re-assert no-sleep every 5s: IDF events (DHCP renew, internal scan,
            // WPA group-key refresh) silently re-enable WIFI_PS_MIN_MODEM, which
            // causes lwIP TCP sends to drop silently and inbound PONGs to be missed.
            if (now - _lastSleepAssertMs >= 5000) {
                WiFi.setSleep(false);
                _lastSleepAssertMs = now;
            }
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
                size_t sent = _tcp.print("PING\n");
                if (sent == 0) {
                    // lwIP rejected the send (modem sleep, broken socket, etc.) —
                    // force reconnect immediately instead of silently missing PINGs.
                    Serial.println("[COMMS] PING send failed — forcing reconnect");
                    _tcp.stop();
                    _budgetStartMs    = now;
                    _connectStartMs   = now;
                    _lastTcpAttemptMs = 0;
                    _state = COMMS_CONNECTING;
                    break;
                }
                _tcp.flush();
                _lastPingMs = now;
            }
            // Anything the base has not confirmed gets another go. Placed after
            // _readLines() so an ACK that arrived this pass is already applied
            // and we do not resend a message that was just confirmed.
            _retryPending();
            break;

        case COMMS_FAILED:
            break;  // unreachable since session 31 — kept for enum completeness
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
bool TimerComms::hasPrepStart()     { bool v = _hasPrepStart;     _hasPrepStart     = false; return v; }
bool TimerComms::hasLandStart()     { bool v = _hasLandStart;     _hasLandStart     = false; return v; }
bool TimerComms::hasScreenCmd()     { bool v = _hasScreenCmd;     _hasScreenCmd     = false; return v; }
bool TimerComms::hasWtSync()        { bool v = _hasWtSync;        _hasWtSync        = false; return v; }

void TimerComms::sendFlight(int pilotId, unsigned long durationMs,
                            int targetS, bool window) {
#ifndef WOKWI_SIM
    char buf[96];
    if (targetS > 0) {
        // Appended, so an older base reads the pilot and duration it always did
        // and ignores the rest. A flight with no target omits them entirely,
        // which is what "no declaration" means on the wire.
        snprintf(buf, sizeof(buf), "FLIGHT pilot=%d dur=%lu target=%d%s",
                 pilotId, durationMs, targetS, window ? " tw=1" : "");
    } else {
        snprintf(buf, sizeof(buf), "FLIGHT pilot=%d dur=%lu", pilotId, durationMs);
    }
    _sendOrQueue(buf);
#endif
}

void TimerComms::sendJumped(int pilotId, unsigned long durationMs) {
#ifndef WOKWI_SIM
    char buf[64];
    snprintf(buf, sizeof(buf), "JUMPED pilot=%d dur=%lu", pilotId, durationMs);
    _sendOrQueue(buf);
#endif
}

void TimerComms::sendScratch(int pilotId, unsigned long durationMs) {
#ifndef WOKWI_SIM
    char buf[64];
    snprintf(buf, sizeof(buf), "SCRATCH pilot=%d dur=%lu", pilotId, durationMs);
    _sendOrQueue(buf);
#endif
}

void TimerComms::sendAltitude(int pilotId, int flightNo, int altM) {
#ifndef WOKWI_SIM
    char buf[64];
    snprintf(buf, sizeof(buf), "ALTITUDE pilot=%d flight=%d alt=%d", pilotId, flightNo, altM);
    _sendOrQueue(buf);
#endif
}

void TimerComms::resendRound(int pilotId, const uint32_t* durations,
                             const int16_t* altitudes, int count) {
#ifndef WOKWI_SIM
    // No pilot means the base cannot attribute any of it; a resend would be
    // discarded exactly like the original was. Nothing useful to do here.
    if (pilotId <= 0 || count <= 0 || durations == nullptr) return;

    Serial.printf("[COMMS] Round reconcile: re-reporting %d flight(s) for pilot=%d\n",
                  count, pilotId);
    for (int i = 0; i < count; ++i) {
        char buf[PENDING_LINE];
        // rc=1 marks this as a reconciliation copy, so the base can tell a
        // recovered flight from a normal one and say so on the run page. Older
        // bases ignore unknown params and treat it as an ordinary FLIGHT.
        snprintf(buf, sizeof(buf), "FLIGHT pilot=%d dur=%lu rc=1",
                 pilotId, (unsigned long)durations[i]);
        _sendOrQueue(buf);
        // 0 = not recorded (F3K, or an F5K flight whose altitude is still to come).
        if (altitudes != nullptr && altitudes[i] > 0) {
            snprintf(buf, sizeof(buf), "ALTITUDE pilot=%d flight=%d alt=%d rc=1",
                     pilotId, i + 1, (int)altitudes[i]);
            _sendOrQueue(buf);
        }
    }
#endif
}

void TimerComms::sendSelect(int pilotId) {
#ifndef WOKWI_SIM
    char buf[32];
    snprintf(buf, sizeof(buf), "SELECT pilot=%d", pilotId);
    _sendOrQueue(buf);
#endif
}

// ── Private — WiFi/TCP ────────────────────────────────────────────────────────

#ifndef WOKWI_SIM

// Send if the socket is genuinely alive, otherwise queue for flush on reconnect.
// _state == COMMS_CONNECTED alone is NOT enough: on a silently dead socket
// (no FIN/RST — e.g. base station power loss) _tcp.connected() stays true for up
// to ~60s and writes are silently discarded by lwIP. The _tcp.connected() check
// catches the explicit-close case immediately; if it fails while we still think
// we're connected, force the reconnect path now instead of waiting for RX timeout.
void TimerComms::_sendOrQueue(const char* line) {
    // Queue FIRST, always — even on what looks like a healthy socket. The entry
    // is only released when the base ACKs it, so a write that vanishes into a
    // silently dead socket is retried instead of lost.
    _enqueue(line);
    if (_state == COMMS_CONNECTED && _tcp.connected()) {
        _sendLine(line);
        if (_pendingCount > 0) {
            PendingMsg& m = _pending[_pendingCount - 1];
            m.lastSentMs = millis();
            m.attempts   = 1;
        }
        return;
    }
    if (_state == COMMS_CONNECTED) {
        Serial.println("[COMMS] Socket dead on send — forcing reconnect");
        unsigned long now = millis();
        _tcp.stop();
        _budgetStartMs    = now;
        _connectStartMs   = now;
        _lastTcpAttemptMs = 0;
        _state = COMMS_CONNECTING;
    }
}

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
        // Send PING immediately so the base's ping timer resets now rather than
        // waiting 30s — guards against the base holding a stale last-ping timestamp
        // from a previous session and firing ping_timeout before our first scheduled PING.
        _sendLine("PING");
        _lastPingMs = millis();
        _flushPending();  // send any messages queued while we were disconnected

    } else if (strncmp(line, "TASK wt=", 8) == 0) {
        _taskWtSeconds = atoi(line + 8);
        const char* disc = strstr(line, "disc=");
        _isF5K = disc && strncmp(disc + 5, "F5K", 3) == 0;

        // Everything below is optional. A pre-v31 base sends none of it, so every
        // field must fall back to what the timer did before. [TF-10]/[TF-11]
        _taskCode[0] = '\0';
        if (const char* t = strstr(line, "task=")) {
            size_t n = 0;
            for (const char* p = t + 5; *p && *p != ' ' && n < sizeof(_taskCode) - 1; p++)
                _taskCode[n++] = *p;
            _taskCode[n] = '\0';
        }

        _targetMode = TARGET_PLAIN;
        if (const char* m = strstr(line, "mode=")) {
            if      (strncmp(m + 5, "poker",  5) == 0) _targetMode = TARGET_POKER;
            else if (strncmp(m + 5, "ladder", 6) == 0) _targetMode = TARGET_LADDER;
            // Anything else, including a mode we have never heard of, stays plain.
        }

        if (const char* v = strstr(line, "start="))   _ladderStartS = atoi(v + 6);
        if (const char* v = strstr(line, "step="))    _ladderStepS  = atoi(v + 5);
        if (const char* v = strstr(line, "targets=")) _pokerTargets = atoi(v + 8);

        // `rungs=60,90,120,150,180` — an explicit ladder. Cleared every TASK so a
        // stepped ladder arriving after an explicit one does not inherit its rungs.
        // ⚠ Deliberately NOT `targets=`: that already carries Poker's target COUNT,
        // and one key cannot be a count in one mode and a list in another.
        _ladderRungCount = 0;
        if (const char* v = strstr(line, "rungs=")) {
            // Walk digits to the end of the parameter. Written this way rather than
            // hunting commas because `rungs=` may be the last param on the line,
            // with no trailing separator to find.
            const char* p = v + 6;
            while (*p && *p != ' ' && _ladderRungCount < MAX_LADDER_RUNGS) {
                if (*p >= '0' && *p <= '9') {
                    _ladderRungs[_ladderRungCount++] = atoi(p);
                    while (*p >= '0' && *p <= '9') p++;
                } else {
                    p++;                       // comma or stray separator
                }
            }
        }

        _hasTaskUpdate = true;
        Serial.printf("[COMMS] Task update: WT=%ds disc=%s task=%s mode=%d\n",
                      _taskWtSeconds, _isF5K ? "F5K" : "F3K",
                      _taskCode[0] ? _taskCode : "?", (int)_targetMode);

    } else if (strncmp(line, "WTSYNC t=", 9) == 0) {
        // The base is the master clock: this many seconds of working time left,
        // as of now. Nothing else about the round changes. [I-51]
        _wtSyncSeconds = atoi(line + 9);
        _hasWtSync     = true;
        Serial.printf("[COMMS] Working time sync: %ds remaining\n", _wtSyncSeconds);

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

    } else if (strncmp(line, "PREP t=", 7) == 0) {
        _prepSeconds  = atoi(line + 7);
        _hasPrepStart = true;
        Serial.printf("[COMMS] Prep start: %ds\n", _prepSeconds);

    } else if (strncmp(line, "LAND t=", 7) == 0) {
        _landSeconds  = atoi(line + 7);
        _hasLandStart = true;
        Serial.printf("[COMMS] Landing window: %ds\n", _landSeconds);

    } else if (strncmp(line, "SCREEN t=", 9) == 0) {
        // Force the display on for N seconds so display work can be checked
        // remotely without someone standing over the timer. 0 = release.
        _screenSeconds = atoi(line + 9);
        _hasScreenCmd  = true;
        Serial.printf("[COMMS] Screen force-on: %ds\n", _screenSeconds);

    } else if (strncmp(line, "ACK ", 4) == 0) {
        _ackPending(line + 4);

    } else if (strcmp(line, "PONG") == 0) {
        // keepalive — _lastRxMs already updated above

    } else {
        Serial.printf("[COMMS] Unknown message: %s\n", line);
    }
}

void TimerComms::_enqueue(const char* line) {
    if (_pendingCount >= PENDING_MAX) {
        // Drop the newest, not the oldest: the older entries have already been
        // attempted and are closer to being confirmed. Either way this is data
        // loss, so it is logged loudly rather than counted silently.
        Serial.printf("[COMMS] Pending buffer FULL (%d) — DROPPING: %s\n", PENDING_MAX, line);
        return;
    }
    PendingMsg& m = _pending[_pendingCount++];
    strncpy(m.line, line, PENDING_LINE - 1);
    m.line[PENDING_LINE - 1] = '\0';
    m.lastSentMs = 0;
    m.attempts   = 0;
    Serial.printf("[COMMS] Pending (%d): %s\n", _pendingCount, line);
}

// (Re)send everything still awaiting an ACK, oldest first. Called on ASSIGN so a
// reconnect replays the backlog, and by _retryPending() on a stalled entry.
void TimerComms::_flushPending() {
    if (_pendingCount == 0) return;
    unsigned long now = millis();
    for (int i = 0; i < _pendingCount; i++) {
        _sendLine(_pending[i].line);
        _pending[i].lastSentMs = now;
        _pending[i].attempts++;
    }
    Serial.printf("[COMMS] Sent %d pending message(s), awaiting ACK\n", _pendingCount);
}

// Base confirmed one message. Match byte-for-byte — the base echoes verbatim
// precisely so this can be an exact comparison rather than a re-parse.
void TimerComms::_ackPending(const char* msg) {
    for (int i = 0; i < _pendingCount; i++) {
        if (strcmp(_pending[i].line, msg) != 0) continue;
        // Close the gap; order of the remainder is preserved.
        for (int j = i; j < _pendingCount - 1; j++) _pending[j] = _pending[j + 1];
        _pendingCount--;
        Serial.printf("[COMMS] ACKed (%d still pending): %s\n", _pendingCount, msg);
        return;
    }
    // Not an error: a retry can be ACKed twice, and the first ACK already
    // removed the entry.
    Serial.printf("[COMMS] ACK for unknown / already-cleared message: %s\n", msg);
}

void TimerComms::_retryPending() {
    if (_pendingCount == 0 || _state != COMMS_CONNECTED || !_tcp.connected()) return;
    unsigned long now = millis();
    for (int i = 0; i < _pendingCount; i++) {
        PendingMsg& m = _pending[i];
        if (m.lastSentMs == 0 || (now - m.lastSentMs) < ACK_RETRY_MS) continue;
        m.lastSentMs = now;
        m.attempts++;
        Serial.printf("[COMMS] No ACK after %lums, retry #%u: %s\n",
                      ACK_RETRY_MS, m.attempts, m.line);
        _sendLine(m.line);
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
