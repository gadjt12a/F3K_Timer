# ESP32 OTA Firmware Update — Field-Tested Reference

Compiled from: Valtrack v4 production OTA (SIM7600 modem, 100+ units), F3K Timer WiFi OTA
design, and ESP-IDF / arduino-esp32 issue tracker research.

Applies to: ESP32, ESP32-S3, ESP32-C3 — Arduino framework via PlatformIO, ESP-IDF, or mixed.

---

## Quick-reference: the rules that matter most

| # | Rule | Why it hurts when broken |
|---|------|--------------------------|
| 1 | Call `mark_app_valid` BEFORE any peripheral that can fail | Rollback loop between two firmware versions |
| 2 | Feed the watchdog in the OTA progress callback | Partition erase takes >5 s — WDT kills it mid-flash |
| 3 | Run OTA in its own FreeRTOS task | AsyncWebServer / display callbacks cause WDT on async_tcp |
| 4 | Erase flash completely when changing partition layout | Stale otadata corrupts new layout, unbootable device |
| 5 | Never change partition table via OTA | Not supported — requires USB flash |
| 6 | Never use GitHub Releases URLs directly | Redirect to S3 (different domain) — ESP32 flashes HTML body |
| 7 | Set `Cache-Control: no-store` on firmware files | Cached old firmware served indefinitely |
| 8 | Keep ota_0 and ota_1 identical in size | Bootloader requirement — mismatch causes unpredictable boot |
| 9 | Do not suspend WiFi / lwIP tasks during download | OTA requires these alive throughout |
| 10 | Plain HTTP is fine on a trusted local network | TLS adds heap pressure and cert expiry risk for no gain |

---

## 1. Library Choice

### Arduino / PlatformIO

**`HTTPUpdate`** — use this for most pull-based WiFi OTA.
- Device fetches firmware from a URL; library handles HTTP + flash write
- Streams directly into flash — does not buffer the full binary in RAM
- Built-in redirect following (but see URL pitfalls below)
- Exposes `onProgress` callback (feed watchdog here)
- Plain HTTP and HTTPS both supported

```cpp
#include <HTTPUpdate.h>
#include <WiFiClient.h>

WiFiClient client;
httpUpdate.onProgress([](int written, int total) {
    esp_task_wdt_reset();  // MUST feed watchdog here
});
t_httpUpdate_return result = httpUpdate.update(client, "http://192.168.4.1:8080/firmware.bin");
```

**`Update` class** — use when you need fine-grained control or a non-HTTP source.
- Foundation that HTTPUpdate wraps
- Also provides `canRollBack()` and `rollBack()` — available regardless of which layer you use
- Use for: MQTT delivery, SD card update, custom binary protocols

**`esp_https_ota`** (ESP-IDF) — use when you need HTTPS with memory constraints.
- Advanced loop mode gives per-chunk callbacks
- `partial_http_download = true` shrinks the mbedTLS receive buffer from 16 KB to 4 KB
- Useful on devices without PSRAM

### Do not use
- `ArduinoOTA` (UDP push from IDE) — fine for dev, do not ship in production
- Blocking OTA inline in AsyncWebServer callbacks — kills the async_tcp watchdog

---

## 2. Partition Table

### Required partitions for OTA

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
ota_0,    app,  ota_0,   0x10000,  0x300000,
ota_1,    app,  ota_1,   0x310000, 0x300000,
```

**Rules:**
- `otadata` must be exactly `0x2000` (8 KB) — two 4 KB flash sectors. Never change size or offset.
- `ota_0` and `ota_1` must be **identical in size**.
- Both OTA slot offsets must be **64 KB aligned** (multiple of `0x10000`).
- NVS stays at `0x9000` — changing its offset or size wipes saved settings.

### Factory partition — include or omit?

| Layout | Pro | Con |
|--------|-----|-----|
| `factory` + `ota_0` + `ota_1` | If both OTA slots are corrupt, USB-flash fallback boots from factory | Wastes as much flash as one OTA slot (typically 1–3 MB) |
| `ota_0` + `ota_1` only | Saves space | If both OTA slots are invalid and you have no serial access, device is bricked |

Recommendation: omit factory if you maintain a known-good USB-flashable binary (e.g. in a `firmware/releases/` archive). Include factory on devices deployed to inaccessible locations.

### 16 MB flash layout (recommended)

```csv
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
ota_0,    app,  ota_0,    0x10000,  0x300000,
ota_1,    app,  ota_1,    0x310000, 0x300000,
littlefs, data, littlefs, 0x610000, 0x9F0000,
```

- 3 MB per OTA slot — comfortable headroom for feature-rich firmware
- ~10 MB LittleFS for logs, assets, config files
- Total: ~12.6 MB of 16 MB used

### platformio.ini

```ini
board_build.flash_size   = 16MB
board_build.partitions   = partitions_ota.csv   ; file in project root
board_build.arduino.memory_type = qio_opi       ; for OPI PSRAM devices (e.g. ESP32-S3R8)
```

### Changing partition layout — always erase first

```powershell
# Erase entire flash before flashing with a new partition table
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env> -t erase
```

Stale `otadata` at the old address will point to OTA slots at wrong offsets. Result:
unbootable device. Always erase first.

### You cannot change the partition table via OTA

The partition table lives at fixed address `0x8000`. Espressif explicitly does not support
changing it over the air. Changing partition layout always requires a USB flash. Plan your
layout before first deployment.

---

## 3. Rollback

### How the ESP32 bootloader handles rollback

Arduino ships with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` baked into its pre-compiled
bootloader. You cannot change this via build flags — it is a bootloader compile-time setting.

**OTA slot states (stored in otadata):**

| State | Bootloader action |
|-------|-------------------|
| `VALID` | Boot it |
| `UNDEFINED` | Boot it (legacy — set by USB flash) |
| `PENDING_VERIFY` | Boot once; if app does not self-confirm, mark `ABORTED` on next boot |
| `INVALID` | Skip, try other slot |
| `ABORTED` | Skip, try other slot |

**Normal OTA flow:**
1. OTA writes to inactive slot → marks it `PENDING_VERIFY`
2. Device reboots into new firmware
3. New firmware calls `esp_ota_mark_app_valid_cancel_rollback()` → state becomes `VALID`
4. Done — new firmware is locked in

**Automatic rollback:**
If the new firmware crashes, hangs, or loses power before step 3 — on next boot the
bootloader marks the slot `ABORTED` and boots the previous slot automatically.

### When to call mark_app_valid — the critical timing rule

```cpp
#include "esp_ota_ops.h"

void setup() {
    // Step 1: initialise essential hardware (display, GPIO, NVS)
    display.begin();
    nvs_init();

    // Step 2: check OTA state
    esp_ota_img_states_t ota_state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // We just OTA'd into this slot. Prove we can boot.
            if (display_ok && nvs_ok) {
                esp_ota_mark_app_valid_cancel_rollback();  // Lock it in
            } else {
                esp_ota_mark_app_invalid_rollback_and_reboot();  // Revert now
            }
        }
    }

    // Step 3: continue init (WiFi, sensors, etc.)
    wifi_init();
}
```

**The rollback loop bug (from Valtrack v4 production):**

Calling `mark_app_valid` AFTER a peripheral that can fail (GSM modem init, WiFi connect,
sensor handshake) causes this sequence:

1. OTA flashes v2 → reboots → v2 starts → modem times out → `mark_app_valid` never called
2. Next boot: bootloader sees `PENDING_VERIFY` → marks `ABORTED` → boots v1
3. v1 starts → modem times out (same field condition) → `mark_app_valid` never called
4. Next boot: v1 is now also `ABORTED` → no valid slots → **bricked**

Fix: mark valid after the display / core hardware confirms the firmware boots, but **before**
any network peripheral that might fail in the field.

### Manual rollback from application code

```cpp
// Check if rollback is available (other slot has valid firmware):
if (Update.canRollBack()) {
    // Show rollback option to user
    Update.rollBack();  // Marks current slot INVALID, reboots to other slot immediately
}

// Or via ESP-IDF directly:
esp_ota_mark_app_invalid_rollback_and_reboot();
```

Rollback is instant — no HTTP download, no re-flash. The previous firmware binary is
untouched in the other OTA slot; only the boot pointer in `otadata` changes.

---

## 4. The Watchdog Problem

`esp_ota_begin()` erases the target partition before writing begins. On a 3 MB partition
this takes several seconds, easily exceeding the default 5-second task watchdog timeout.

Tracked in: arduino-esp32 issues #3775, #3528, #6606. No library-level fix as of 2024.

**Fix — feed in the progress callback:**

```cpp
Update.onProgress([](size_t written, size_t total) {
    esp_task_wdt_reset();
    // Update a progress bar / arc here if needed
    int pct = (total > 0) ? (written * 100 / total) : 0;
    display_update_progress(pct);
});
```

**Also: run OTA in its own FreeRTOS task.**

The callback runs synchronously in the calling task. If you call `httpUpdate.update()` from
inside an `AsyncWebServer` handler or an interrupt-driven callback, the async_tcp task
starves and its own watchdog fires — killing the download from a completely different task.

```cpp
struct OtaParams { const char* url; };

void ota_task(void* pv) {
    OtaParams* p = (OtaParams*)pv;
    WiFiClient client;

    Update.onProgress([](size_t w, size_t t) {
        esp_task_wdt_reset();
        display_update_progress(t > 0 ? w * 100 / t : 0);
    });

    t_httpUpdate_return res = httpUpdate.update(client, p->url);

    if (res == HTTP_UPDATE_OK) {
        ESP.restart();
    }
    // Handle error...
    delete p;
    vTaskDelete(NULL);
}

// Trigger OTA:
OtaParams* params = new OtaParams{ "http://192.168.4.1:8080/firmware.bin" };
xTaskCreate(ota_task, "OTA", 8192, params, 5, NULL);
```

**Stack size:**
- Plain HTTP: 8192 bytes minimum
- HTTPS: 10240 bytes recommended
- Check actual usage: `uxTaskGetStackHighWaterMark(NULL)` at the end of a download

**Do not suspend during OTA:**
- WiFi driver tasks
- lwIP TCP/IP task

These must remain running for the download to proceed. You can suspend unrelated tasks
(audio DMA, sensor polling) to reduce load if needed.

---

## 5. HTTP vs HTTPS

### Plain HTTP — use on trusted local networks

No certificates, no TLS heap overhead, no cert expiry risk. For devices connecting to a
known base station or local AP, plain HTTP is a valid production choice.

```cpp
WiFiClient client;  // plain HTTP
httpUpdate.update(client, "http://192.168.4.1:8080/firmware.bin");
```

### HTTPS — use for internet deployments

**Do NOT hardcode a CA certificate PEM string.** CA certs expire (5–10 years). When they
do, every device in the field stops being able to OTA update — potentially permanently.

**Use Espressif's certificate bundle instead:**

```cpp
#include "esp_crt_bundle.h"

WiFiClientSecure client;
client.setCACertBundle(esp_crt_bundle_attach);  // ~130 trusted root CAs, auto-updated with firmware
httpUpdate.update(client, "https://example.com/firmware.bin");
```

**Never use `setInsecure()` in production.** It disables all certificate verification.

**HTTPS memory note:** TLS handshake + receive buffer = ~16 KB contiguous RAM by default.
On memory-constrained devices (no PSRAM), use `esp_https_ota` with
`partial_http_download = true` + `max_http_request_size = 4096` to reduce to ~4 KB.
On devices with PSRAM (e.g. ESP32-S3R8), this is less of a concern.

---

## 6. URLs and Server Setup

### URL pitfalls

**GitHub Releases URLs — do not use directly.**
GitHub Releases redirects to AWS S3 (a different domain). `HTTPUpdate`'s redirect following
downloads the HTTP redirect response body (HTML) into flash instead of the binary. The HTML
starts with `<`, not the ESP32 magic byte `0xE9`. Result: "invalid image" or "wrong magic
byte" error.

Use a direct static file URL that terminates without a cross-domain redirect.

**What a safe URL looks like:**
```
http://192.168.4.1:8080/firmware.bin        # local AP — ideal
http://my-server.local/ota/firmware.bin     # mDNS on LAN — fine
https://storage.example.com/firmware.bin    # direct S3/Spaces/R2 URL — fine
```

**What a broken URL looks like:**
```
https://github.com/user/repo/releases/download/v1.0/firmware.bin  # redirects to S3 (diff domain)
```

### Server configuration (nginx)

```nginx
server {
    listen 80;
    root /firmware;

    location = /version.json {
        default_type application/json;
        add_header Cache-Control "no-store";
    }

    location ~* \.bin$ {
        default_type application/octet-stream;
        add_header Cache-Control "no-store";
    }

    location / { return 404; }
}
```

`Cache-Control: no-store` on both files is mandatory. Without it, a caching proxy or the
device's own HTTP stack can serve stale firmware indefinitely.

### Version manifest — keep it minimal

```json
{"version": "fw-v10", "size": 838192}
```

- `version`: string the device compares to its own `FW_VERSION` constant
- `size`: optional, allows displaying progress before download starts
- MD5/SHA256: not needed in the manifest — `Update` class validates internally

---

## 7. Download Reliability

### Single session — do not use Range requests for chunked download

Fetching firmware in multiple HTTP Range requests (e.g. one request per 4 KB chunk) is
unreliable over any non-trivial network. Each request has its own TCP handshake, and on
cellular/LTE a 165-chunk download at 5 s/request = 14 minutes with many failure points.

`HTTPUpdate` already does this correctly: one connection, streams the full binary. Do not
re-implement chunked downloading unless you have a specific reason.

**Lesson from Valtrack v4:** switching from 165 per-chunk HTTP requests to a single
download session (using SIM7600 modem buffering) was the single biggest reliability
improvement — from frequent mid-download failures to near-100% success.

### No resume support

`HTTPUpdate` does not support resuming a partial download. If WiFi drops at 90% through
a 1 MB download, the entire download restarts from zero on the next attempt. The partially
written partition is marked `INVALID` automatically — safe, will not be booted.

Design for retry-from-zero: make sure your OTA trigger can be re-run (user button, next
scheduled check, etc.).

### WiFi reconnect before retry

```cpp
if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(500);
    }
}
if (WiFi.status() == WL_CONNECTED) {
    // retry OTA
}
```

---

## 8. Version Embedding

Embed the firmware version as a compile-time constant and stamp it from your release script.

```cpp
// include/config.h
#define FW_VERSION "fw-v10"
```

Report it in log output at boot:
```cpp
Serial.printf("Firmware: %s\n", FW_VERSION);
```

Report it to a server if your device phones home — useful for tracking fleet versions.

**Stamp automatically in release script (PowerShell example):**
```powershell
(Get-Content "include/config.h") -replace '#define FW_VERSION ".*"', "#define FW_VERSION `"$ver`"" |
    Set-Content "include/config.h"
```

---

## 9. Common Error Codes and What They Mean

| Error | Cause | Fix |
|-------|-------|-----|
| `UPDATE_ERROR_NO_PARTITION` | No OTA partition in partition table | Use OTA-capable partition table |
| `UPDATE_ERROR_SIZE` | Binary larger than OTA partition | Increase OTA partition size |
| `UPDATE_ERROR_MAGIC_BYTE` | Downloaded HTML or redirect body instead of firmware | Use direct URL, check for redirects |
| `UPDATE_ERROR_MD5` | Corrupted download, truncated stream, or wrong file | Retry; check server is serving correct file |
| `UPDATE_ERROR_WRITE` | Flash write failed | Erase entire flash; check for flash wear |
| `UPDATE_ERROR_ERASE` | Partition erase failed | Hardware fault or wrong partition address |
| `UPDATE_ERROR_SPACE` | Not enough space | Partition too small |
| `HTTP_UPDATE_FAILED` (general) | Any HTTP / Update error | Check `httpUpdate.getLastError()` + `httpUpdate.getLastErrorString()` |

**Debugging otadata:**
```powershell
# Read otadata partition to inspect slot states:
esptool.py --chip esp32s3 --port COM4 read_flash 0xe000 0x2000 otadata_dump.bin
# Then hexdump to inspect: bytes 0-3 of each 4KB sector show slot sequence numbers
```

---

## 10. First-Time OTA Deployment Checklist

When adding OTA to an existing project that previously had no OTA support:

- [ ] Design and document the partition layout — finalise before first deployment
- [ ] Add `otadata` and dual OTA slot partitions to the CSV
- [ ] Add `board_build.partitions = your_table.csv` to platformio.ini
- [ ] Add `FW_VERSION` constant to firmware
- [ ] Add `mark_app_valid` call in `setup()` (with health check gate)
- [ ] Add watchdog feed to OTA progress callback
- [ ] Add OTA in a dedicated FreeRTOS task
- [ ] Set up version endpoint on server with `Cache-Control: no-store`
- [ ] **Erase entire flash** before the first USB flash with new partition table
- [ ] USB flash the OTA-capable firmware (this is the one-time manual step)
- [ ] Verify the device boots and `mark_app_valid` is called (check serial output)
- [ ] Test OTA update end-to-end at least twice before field deployment
- [ ] Test rollback: OTA to intentionally broken firmware, confirm auto-rollback works
- [ ] Archive the known-good USB-flashable binary somewhere safe

---

## 11. Modem-Based OTA (SIM7600 / A7672 / SIM800)

For cellular-connected devices using AT command modems instead of WiFi.

The modem's internal HTTP buffer changes the problem significantly:

```
AT+HTTPACTION=0        # Single GET — modem downloads entire binary into its buffer
+HTTPACTION: 0,200,838192   # Reports HTTP status and content length

AT+HTTPREAD=0,4096    # Read first 4096 bytes from modem buffer into ESP32
AT+HTTPREAD=4096,4096 # Read next chunk...
AT+HTTPTERM           # Close HTTP session after final chunk
```

**Advantages:**
- ESP32 does not buffer the entire binary — reads it sequentially from modem memory
- Single TCP connection lifecycle — no per-chunk reconnect overhead
- Modem handles TLS if URL is HTTPS — ESP32 never sees certificates

**Pitfalls specific to modem OTA:**

`+HTTPREAD: 0` residual — the modem sends a zero-length header as an end-of-response
marker. This can arrive after the inter-chunk flush window and be misinterpreted as a
zero-length data block, aborting the download. Skip up to 3 consecutive zero-length
headers before treating it as a real abort condition.

Suspend your UART event task during binary read:
```c
vTaskSuspend(uart_event_task_handle);
uart_flush_input(UART_PORT_NUM);
// ... binary read loop ...
vTaskResume(uart_event_task_handle);
```

Add 200 ms delay + `uart_flush_input()` between chunks to drain `+HTTPREAD: 0\r\nOK\r\n`
residuals from the modem.

---

## 12. Things That Seem Fine But Are Not

| Pattern | Why it breaks |
|---------|---------------|
| `delay(1000)` in OTA task instead of watchdog feed | Watchdog still fires — `delay()` yields the task but does not reset the WDT |
| Calling OTA from `loop()` without a task | Blocks `loop()` forever; no watchdog feed; crashes on long downloads |
| Matching version string by prefix (e.g. `strncmp`) | "fw-v10" matches "fw-v1" — always use exact string compare |
| Storing OTA URL in NVS and pointing to GitHub Releases | Redirect URL stored forever; CA cert embedded in firmware expires |
| OTA partition smaller than current firmware + a bit of headroom | Works today, fails after the next feature addition; leave 2x+ headroom |
| `Update.end()` without checking return value | Silent failure — partition not marked bootable, old firmware runs after reboot |
| Testing OTA only in one direction (old→new) | New→older downgrade often has different bugs; test both directions |
| Assuming PSRAM is used for TLS buffers automatically | TLS stack uses internal RAM by default; must explicitly configure `heap_caps_malloc` calls or use `partial_http_download` |
