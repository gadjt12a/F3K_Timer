#ifdef WAVESHARE_HW

#include "OtaUpdater.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_task_wdt.h>
#include "fw_version.h"

// Release number from a version string: "fw-v28" -> 28, or -1 if unparseable.
//
// The check used to be `strcmp(ver, FW_VERSION) != 0`, i.e. *any* difference
// meant "an update is available" — so a base station holding an old build would
// offer to take this timer backwards, with nothing on screen to say so. That is
// a real field risk: a CD updates the timers, forgets the Pi, and then "updates"
// a timer straight back onto older firmware mid-competition.
//
// Note this must compare numbers, not strings: "fw-v9" sorts above "fw-v28"
// lexically, so a string compare would be worse than the equality test it
// replaces. [I-41]
static int _fwNum(const char* ver) {
    if (!ver) return -1;
    if (strncmp(ver, "fw-v", 4) != 0) return -1;
    const char* p = ver + 4;
    if (*p == '\0') return -1;
    int n = 0;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        n = n * 10 + (*p - '0');
    }
    return n;
}

// Extract "version" field from {"version":"fw-v10",...}
static bool _parseVersionJson(const char* json, char* out, size_t len) {
    const char* key = "\"version\":\"";
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    const char* end = strchr(p, '"');
    if (!end) return false;
    size_t n = (size_t)(end - p);
    if (n >= len) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

void OtaUpdater::begin() {
    _status   = (uint8_t)OTA_IDLE;
    _progress = 0;
    _availVer[0] = '\0';
}

void OtaUpdater::check() {
    if ((OtaStatus)_status == OTA_DOWNLOADING) return;
    // Re-entering the OTA screen calls check() again, so without this a check
    // still in flight gets a second task racing it for the same _status.
    if ((OtaStatus)_status == OTA_CHECKING) {
        Serial.println("[OTA] Check already in flight, ignoring");
        return;
    }
    _status    = (uint8_t)OTA_CHECKING;
    _availVer[0] = '\0';
    // 8192, not 4096: HTTPClient plus the String returned by getString() is a lot
    // for a 4 KB stack, and an overflow here aborts the task silently, leaving
    // _status on CHECKING forever with no way back out of the screen.
    if (xTaskCreate(_checkTask, "OTA_CHK", 8192, this, 3, nullptr) != pdPASS) {
        Serial.println("[OTA] Could not create check task -> OTA_FAILED");
        _status = (uint8_t)OTA_FAILED;
    }
}

// The OTA screen is reachable in well under the ~20 s the radio takes to
// associate after a boot, and check() runs once on entry — so arriving early
// left a red NO WIFI on screen that never cleared, and the only way to retry was
// to back out and walk the four settings holds again. Now the check re-fires by
// itself the moment WiFi is up.
void OtaUpdater::retryIfWifiReturned() {
    if ((OtaStatus)_status != OTA_NO_WIFI) return;
    if (WiFi.status() != WL_CONNECTED)     return;
    check();
}

void OtaUpdater::startUpdate() {
    if ((OtaStatus)_status != OTA_AVAILABLE) return;
    _status   = (uint8_t)OTA_DOWNLOADING;
    _progress = 0;
    xTaskCreate(_updateTask, "OTA_UPD", 8192, this, 5, nullptr);
}

void OtaUpdater::forceUpdate() {
    // No status gate except "not already downloading" — the base has decided.
    if ((OtaStatus)_status == OTA_DOWNLOADING) return;
    Serial.println("[OTA] Forced update by base station push");
    _status   = (uint8_t)OTA_DOWNLOADING;
    _progress = 0;
    xTaskCreate(_updateTask, "OTA_UPD", 8192, this, 5, nullptr);
}

void OtaUpdater::_checkTask(void* pv) {
    OtaUpdater* self = (OtaUpdater*)pv;

    // This ran with no logging at all until session 64, where a check stuck on
    // CHECKING forever could only be diagnosed from the base station's HTTP log —
    // which showed 200 OK, proving the request succeeded but saying nothing about
    // what happened next. Every exit from here now announces itself, so a stuck
    // check is readable from the serial log alone.
    Serial.printf("[OTA] Check start, free heap=%u, stack hwm=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] No WiFi -> OTA_NO_WIFI");
        self->_status = (uint8_t)OTA_NO_WIFI;
        vTaskDelete(nullptr);
        return;
    }

    {
        // Scoped so WiFiClient/HTTPClient destruct before vTaskDelete — deleting
        // the running task skips destructors, leaking the socket every check.
        WiFiClient client;
        HTTPClient http;
        http.setTimeout(8000);
        http.begin(client, OTA_VERSION_URL);
        http.addHeader("Cache-Control", "no-cache");
        int code = http.GET();
        Serial.printf("[OTA] GET %s -> %d\n", OTA_VERSION_URL, code);

        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.printf("[OTA] Payload (%u bytes): %s\n",
                          (unsigned)payload.length(), payload.c_str());
            char ver[32];
            if (_parseVersionJson(payload.c_str(), ver, sizeof(ver))) {
                strncpy(self->_availVer, ver, sizeof(self->_availVer) - 1);
                self->_availVer[sizeof(self->_availVer) - 1] = '\0';

                // Only a strictly newer build is an update. Equal is up to date;
                // older means the *base station* is stale, and we say so rather
                // than offering to flash backwards. Unparseable on either side
                // falls back to "different means available" so a future naming
                // scheme cannot silently disable updates altogether.
                int remote = _fwNum(ver);
                int local  = _fwNum(FW_VERSION);
                const char* why;
                if (remote < 0 || local < 0) {
                    bool same = (strcmp(ver, FW_VERSION) == 0);
                    self->_status = (uint8_t)(same ? OTA_UP_TO_DATE : OTA_AVAILABLE);
                    why = same ? "UP_TO_DATE (unparseable, equal)"
                               : "AVAILABLE (unparseable, differs)";
                } else if (remote > local) {
                    self->_status = (uint8_t)OTA_AVAILABLE;
                    why = "AVAILABLE";
                } else if (remote == local) {
                    self->_status = (uint8_t)OTA_UP_TO_DATE;
                    why = "UP_TO_DATE";
                } else {
                    self->_status = (uint8_t)OTA_BASE_OLDER;
                    why = "BASE_OLDER (refusing downgrade)";
                }
                Serial.printf("[OTA] Offered=%s running=%s -> %s\n",
                              ver, FW_VERSION, why);
            } else {
                Serial.println("[OTA] Payload did not parse -> OTA_FAILED");
                self->_status = (uint8_t)OTA_FAILED;
            }
        } else {
            Serial.printf("[OTA] HTTP %d -> OTA_FAILED\n", code);
            self->_status = (uint8_t)OTA_FAILED;
        }

        http.end();
    }

    Serial.printf("[OTA] Check done, status=%d, stack hwm=%u\n",
                  (int)self->_status,
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

void OtaUpdater::_updateTask(void* pv) {
    OtaUpdater* self = (OtaUpdater*)pv;

    WiFiClient client;

    // Feed watchdog during erase + write; this also feeds it before the first
    // progress callback fires (during the flash erase phase).
    httpUpdate.onProgress([self](int cur, int total) {
        esp_task_wdt_reset();
        if (total > 0) self->_progress = (cur * 100) / total;
    });

    // Do not auto-reboot — we set status then restart explicitly so the
    // display can show "REBOOTING" before the device resets.
    httpUpdate.rebootOnUpdate(false);

    esp_task_wdt_reset();  // reset WDT before the potentially long erase phase
    t_httpUpdate_return res = httpUpdate.update(client, OTA_FIRMWARE_URL);

    switch (res) {
        case HTTP_UPDATE_OK:
            self->_status = (uint8_t)OTA_SUCCESS;
            delay(500);
            ESP.restart();
            break;
        default:
            // HTTP_UPDATE_FAILED or HTTP_UPDATE_NO_UPDATES
            self->_status = (uint8_t)OTA_FAILED;
            break;
    }

    vTaskDelete(nullptr);
}

#endif // WAVESHARE_HW
