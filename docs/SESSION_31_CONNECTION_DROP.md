# Session 31 — Field Connection-Drop Fixes (TimerComms)

Status: implemented, builds clean, **not yet field-tested**. Committed but review the
test plan below before the next competition.

## Problem

In-field WiFi/TCP drops between timer and base station caused:

1. **Lost flight records** — on a silently dead socket (base station power loss, no
   FIN/RST) `WiFiClient::connected()` stays `true` for up to ~60s and lwIP silently
   discards writes. `sendFlight()` used `_state == COMMS_CONNECTED` as its only gate,
   so flights stopped in that window were sent to nowhere and never queued.
2. **Permanent standalone mode** — after 5 minutes of failed reconnect the state
   machine parked in `COMMS_FAILED` (terminal). A base station outage longer than
   5 min meant the timer needed a manual reboot to ever reconnect.
3. **Slow dead-socket detection** — `RX_TIMEOUT_MS` was 90s (three PING intervals).

Research basis (Sonnet agent, session 31): `connected()` only detects explicit
close; silent death needs RX-timeout detection; `WiFi.setSleep(false)` and the full
`WIFI_OFF → STA → begin()` recovery cycle already in the code are correct and were
kept unchanged. App-level PING/PONG beats TCP SO_KEEPALIVE (lwIP default ~2h probes).

## Changes (all in `src/comms/TimerComms.{h,cpp}`)

| Change | Where |
|---|---|
| Budget expiry no longer terminal — logs, resets `_budgetStartMs`, retries forever | `update()` COMMS_CONNECTING case |
| `COMMS_FAILED` now unreachable (enum kept so `baseConnState()` switch still compiles) | header comment + case |
| `RX_TIMEOUT_MS` 90000 → 45000 (one missed 30s PONG + 15s grace) | header |
| New `_sendOrQueue()`: sends only if `_state == COMMS_CONNECTED && _tcp.connected()`; otherwise enqueues, and if state said connected but socket is dead it forces an immediate transition to COMMS_CONNECTING | new private method; `sendFlight`/`sendAltitude`/`sendSelect` now call it |

Unchanged by design: `WiFi.setSleep(false)`, full OFF→STA recovery cycle with
`delay(100)`, 30s PING interval, 16-slot pending ring buffer, `_flushPending()` on
ASSIGN.

## Known residual gap

A message sent while the socket is *silently* dead but `_tcp.connected()` still
returns `true` is still lost — no send-side detection exists in lwIP for this case.
The 45s RX timeout narrows the window; true closure would need base-side ACKs per
FLIGHT message (protocol change, not done).

## Field test plan (next session)

1. Normal round with base station — confirm no regression (FLIGHT arrives live).
2. Kill base station AP mid-round, stop a flight → serial should show
   `Queued (offline)` then `Socket dead on send — forcing reconnect`; restart AP →
   after ASSIGN, `Flushed N queued messages` and flight appears at base.
3. Leave base station off >5 min → serial shows repeating
   `5-minute budget elapsed — restarting connect cycle`; power AP back on →
   timer reconnects without reboot.
4. Idle connected timer, pull AP power (silent death) → reconnect attempt should
   start within ~45s (watch for `TCP dropped: ... rxAge=`).
