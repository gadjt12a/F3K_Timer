#ifdef WAVESHARE_HW

#include "OtaUpdater.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_task_wdt.h>
#include "fw_version.h"

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
    _status    = (uint8_t)OTA_CHECKING;
    _availVer[0] = '\0';
    xTaskCreate(_checkTask, "OTA_CHK", 4096, this, 3, nullptr);
}

void OtaUpdater::startUpdate() {
    if ((OtaStatus)_status != OTA_AVAILABLE) return;
    _status   = (uint8_t)OTA_DOWNLOADING;
    _progress = 0;
    xTaskCreate(_updateTask, "OTA_UPD", 8192, this, 5, nullptr);
}

void OtaUpdater::_checkTask(void* pv) {
    OtaUpdater* self = (OtaUpdater*)pv;

    if (WiFi.status() != WL_CONNECTED) {
        self->_status = (uint8_t)OTA_NO_WIFI;
        vTaskDelete(nullptr);
        return;
    }

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(client, OTA_VERSION_URL);
    http.addHeader("Cache-Control", "no-cache");
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
        String payload = http.getString();
        char ver[32];
        if (_parseVersionJson(payload.c_str(), ver, sizeof(ver))) {
            strncpy(self->_availVer, ver, sizeof(self->_availVer) - 1);
            self->_availVer[sizeof(self->_availVer) - 1] = '\0';
            self->_status = (uint8_t)(strcmp(ver, FW_VERSION) == 0
                                       ? OTA_UP_TO_DATE : OTA_AVAILABLE);
        } else {
            self->_status = (uint8_t)OTA_FAILED;
        }
    } else {
        self->_status = (uint8_t)OTA_FAILED;
    }

    http.end();
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
