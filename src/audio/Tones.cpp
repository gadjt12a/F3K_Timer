#include "Tones.h"
#include "pin_config.h"

#ifdef WOKWI_SIM
// Wokwi simulation uses Arduino tone() with a passive buzzer
static unsigned long _wokwiToneEndMs = 0;
static bool _wokwiTonePlaying = false;
#endif

#ifdef WAVESHARE_HW
#include <driver/i2s.h>
#include <Wire.h>
#include <math.h>

// ES8311 Register addresses
#define ES8311_REG_RESET        0x00
#define ES8311_REG_CLK_MGR      0x01
#define ES8311_REG_CLK_DIV1     0x02
#define ES8311_REG_CLK_DIV2     0x03
#define ES8311_REG_CLK_ADC_OSR  0x04
#define ES8311_REG_CLK_DAC_OSR  0x05
#define ES8311_REG_CLK_ADC_SEL  0x06
#define ES8311_REG_CLK_DAC_SEL  0x07
#define ES8311_REG_SYS_CTRL     0x0D
#define ES8311_REG_SDPOUT       0x0A
#define ES8311_REG_SDPIN        0x09
#define ES8311_REG_ADC_CTRL     0x17
#define ES8311_REG_DAC_CTRL     0x32
#define ES8311_REG_HP_CTRL      0x33
#define ES8311_REG_DAC_VOL      0x32
#define ES8311_REG_GPIO         0x44
#define ES8311_REG_GP_STAT      0x45

static const int SAMPLE_RATE = 16000;  // Lower rate, more compatible
static const int DMA_BUF_COUNT = 8;
static const int DMA_BUF_LEN = 256;
static const int SINE_TABLE_SIZE = 256;

QueueHandle_t Tones::_toneQueue = nullptr;
TaskHandle_t  Tones::_toneTask = nullptr;
int16_t*      Tones::_sineBuf = nullptr;
bool          Tones::_taskAmpOn = false;

// ES8311 I2C helper functions
static bool es8311_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ADDR_ES8311);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static uint8_t es8311_read_reg(uint8_t reg) {
    Wire.beginTransmission(ADDR_ES8311);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ADDR_ES8311, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static bool es8311_init() {
    // Check if ES8311 is present
    Wire.beginTransmission(ADDR_ES8311);
    if (Wire.endTransmission() != 0) {
        Serial.println("[TONE] ES8311 not found on I2C");
        return false;
    }
    Serial.println("[TONE] ES8311 found on I2C");

    // Based on ESP-BSP es8311.c driver with coefficient table
    // For 16kHz sample rate @ MCLK=4096000Hz (256*fs):
    // pre_div=0x01, pre_multi=0x00, adc_div=0x01, dac_div=0x01
    // fs_mode=0x00, lrck_h=0x00, lrck_l=0xff, bclk_div=0x04
    // adc_osr=0x10, dac_osr=0x10

    // STEP 1: Full reset sequence
    es8311_write_reg(0x00, 0x1F);  // Reset all digital
    delay(20);
    es8311_write_reg(0x00, 0x00);  // Clear reset
    delay(20);
    es8311_write_reg(0x00, 0x80);  // CSM_ON - power state machine on
    delay(50);

    // STEP 2: Clock manager
    // Reg01: bit7=0 (MCLK from MCLK pin), bit6=0 (not inverted), bits5:0=0x3F (all clks on)
    es8311_write_reg(0x01, 0x3F);

    // STEP 3: Clock dividers from coefficient table for 16kHz @ 4.096MHz MCLK
    // Reg02: bits[7:5]=pre_div-1, bits[4:3]=pre_multi, bits[2:0]=adc_div-1
    // pre_div=1 -> 0, pre_multi=0, adc_div=1 -> 0 = 0x00
    uint8_t reg02 = ((0x01 - 1) << 5) | (0x00 << 3) | (0x01 - 1);
    es8311_write_reg(0x02, reg02);  // 0x00

    // Reg03: bits[7:4]=adc_osr, bits[3:0]=reserved
    es8311_write_reg(0x03, 0x10);  // ADC OSR

    // Reg04: bits[7:4]=dac_osr, bits[3:0]=reserved
    es8311_write_reg(0x04, 0x10);  // DAC OSR

    // Reg05: bits[7:4]=dac_div-1, bits[3:0]=fs_mode
    uint8_t reg05 = ((0x01 - 1) << 4) | 0x00;
    es8311_write_reg(0x05, reg05);  // 0x00

    // Reg06: bits[7:5]=reserved, bit4=bclk_inv, bits[3:0]=bclk_div
    es8311_write_reg(0x06, 0x04);  // BCLK divider

    // Reg07/08: LRCK divider (16-bit value)
    es8311_write_reg(0x07, 0x00);  // LRCK_H
    es8311_write_reg(0x08, 0xFF);  // LRCK_L

    // STEP 4: I2S format - 16-bit, standard I2S
    // bits[1:0]=00 for I2S standard, bits[4:2]=011 for 16-bit = 0x0C
    es8311_write_reg(0x09, 0x0C);  // SDPIN format
    es8311_write_reg(0x0A, 0x0C);  // SDPOUT format

    // STEP 5: System registers
    es8311_write_reg(0x0B, 0x00);
    es8311_write_reg(0x0C, 0x00);

    // STEP 6: Power up analog (from ESP-ADF)
    es8311_write_reg(0x0D, 0x01);  // Power up analog
    es8311_write_reg(0x0E, 0x02);  // Enable PGA and ADC modulator
    es8311_write_reg(0x12, 0x00);  // Power up DAC
    es8311_write_reg(0x13, 0x10);  // Enable output, HP switch on

    // STEP 7: ADC/DAC config
    es8311_write_reg(0x1C, 0x6A);  // ADC: EQ bypass, DC offset cancel
    es8311_write_reg(0x37, 0x08);  // DAC: EQ bypass

    delay(100);  // Let everything stabilize

    // STEP 8: Set volume and unmute
    es8311_write_reg(0x32, 0xBF);  // DAC volume (near max)
    es8311_write_reg(0x31, 0x00);  // DAC unmute

    // Verify critical registers
    Serial.println("[TONE] ES8311 register verification:");
    Serial.printf("  0x00 (CSM): 0x%02X (expect 0x80)\n", es8311_read_reg(0x00));
    Serial.printf("  0x01 (CLK): 0x%02X (expect 0x3F)\n", es8311_read_reg(0x01));
    Serial.printf("  0x0D (pwr): 0x%02X (expect 0x01)\n", es8311_read_reg(0x0D));
    Serial.printf("  0x12 (DAC): 0x%02X (expect 0x00)\n", es8311_read_reg(0x12));
    Serial.printf("  0x14 (out): 0x%02X (expect 0x10)\n", es8311_read_reg(0x14));
    Serial.printf("  0x31 (mute): 0x%02X (expect 0x00)\n", es8311_read_reg(0x31));
    Serial.printf("  0x32 (vol): 0x%02X (expect 0x00)\n", es8311_read_reg(0x32));
    Serial.printf("  0x37 (cfg): 0x%02X (expect 0x48)\n", es8311_read_reg(0x37));

    Serial.println("[TONE] ES8311 initialized");
    return true;
}
#endif

void Tones::begin() {
    _count = 0;

#ifdef WOKWI_SIM
    pinMode(BUZZER_SIM, OUTPUT);
    Serial.println("[TONE] Wokwi buzzer initialized on GPIO13");
#endif

#ifdef WAVESHARE_HW
    Serial.println("[TONE] Initializing audio...");

    // Wait for ALDO3 power stabilization (enabled in Buttons::begin)
    delay(50);

    pinMode(PA_EN, OUTPUT);
    digitalWrite(PA_EN, LOW);

    // Sine lookup table — small enough for internal RAM
    // Use maximum amplitude (32767) for loudest output
    _sineBuf = (int16_t*)malloc(SINE_TABLE_SIZE * sizeof(int16_t));
    if (_sineBuf) {
        for (int i = 0; i < SINE_TABLE_SIZE; i++) {
            _sineBuf[i] = (int16_t)(sinf(2.0f * M_PI * i / SINE_TABLE_SIZE) * 32000);
        }
        Serial.printf("[TONE] Sine table created, first values: %d, %d, %d\n",
                      _sineBuf[0], _sineBuf[1], _sineBuf[64]);
    }

    // CRITICAL: Install I2S driver FIRST to provide MCLK to ES8311
    // ES8311 requires MCLK present for clock configuration to work
    i2s_config_t i2s_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = true,  // Use APLL for accurate clocking
        .tx_desc_auto_clear = true,
        .fixed_mclk = SAMPLE_RATE * 256  // MCLK = 256 * fs
    };
    Serial.printf("[TONE] I2S config: %d Hz, stereo I2S, MCLK=%d\n", SAMPLE_RATE, SAMPLE_RATE * 256);

    i2s_pin_config_t pin_cfg = {
        .mck_io_num = I2S_MCLK,
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DO,
        .data_in_num = I2S_DI
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[TONE] I2S driver install failed: %d\n", err);
        _audioReady = false;
        return;
    }
    Serial.println("[TONE] I2S driver installed");

    err = i2s_set_pin(I2S_NUM_0, &pin_cfg);
    if (err != ESP_OK) {
        Serial.printf("[TONE] I2S set pin failed: %d\n", err);
        _audioReady = false;
        return;
    }
    Serial.println("[TONE] I2S pins configured");

    // Wait for I2S clocks to stabilize before configuring ES8311
    delay(50);

    // NOW initialize ES8311 codec (MCLK is present)
    if (!es8311_init()) {
        Serial.println("[TONE] ES8311 init failed - audio disabled");
        _audioReady = false;
        return;
    }

    _audioReady = (_sineBuf != nullptr);

    // Create queue and background task for non-blocking playback
    _toneQueue = xQueueCreate(8, sizeof(ToneCommand));
    xTaskCreatePinnedToCore(
        _toneTaskFunc,
        "ToneTask",
        4096,
        this,
        1,    // low priority
        &_toneTask,
        0     // core 0 (loop runs on core 1)
    );

    Serial.println("[TONE] I2S audio ready (background task)");
#endif
}

void Tones::_clear() {
    _count = 0;
}

void Tones::_add(unsigned long atMs, uint32_t freq, uint32_t durMs) {
    if (_count >= QUEUE) return;
    _ev[_count++] = {atMs, freq, durMs, false};
}

void Tones::silence() {
    _clear();
#ifdef WAVESHARE_HW
    // Clear any pending tones in the queue
    if (_toneQueue) {
        xQueueReset(_toneQueue);
    }
#endif
}

#ifdef WAVESHARE_HW
void Tones::_ampEnable(bool on) {
    if (on != _taskAmpOn) {
        digitalWrite(PA_EN, on ? HIGH : LOW);
        _taskAmpOn = on;
        if (on) delay(50);  // Amp needs time to stabilize
    }
}

void Tones::_toneTaskFunc(void* param) {
    Tones* self = (Tones*)param;
    ToneCommand cmd;

    while (true) {
        if (xQueueReceive(_toneQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            self->_playToneBlocking(cmd.freqHz, cmd.durationMs);
        }
    }
}

void Tones::_playToneBlocking(uint32_t freqHz, uint32_t durationMs) {
    Serial.printf("[TONE] Playing tone: %d Hz, %d ms\n", freqHz, durationMs);
    if (!_audioReady || !_sineBuf || freqHz == 0) {
        Serial.println("[TONE] Cannot play - not ready");
        return;
    }

    _ampEnable(true);
    Serial.println("[TONE] Amp enabled, sending I2S data...");

    uint32_t totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    float phaseIncrement = (float)freqHz * SINE_TABLE_SIZE / SAMPLE_RATE;
    float phase = 0;

    // Stereo output - L and R interleaved
    static int16_t buf[DMA_BUF_LEN * 2];
    uint32_t samplesWritten = 0;
    size_t bytesWritten;

    Serial.printf("[TONE] Generating %d samples, phase inc=%.3f\n", totalSamples, phaseIncrement);

    while (samplesWritten < totalSamples) {
        int samplesToWrite = min((uint32_t)(DMA_BUF_LEN), totalSamples - samplesWritten);

        for (int i = 0; i < samplesToWrite; i++) {
            int idx = (int)phase % SINE_TABLE_SIZE;
            int16_t sample = _sineBuf[idx];
            buf[i * 2]     = sample;  // Left
            buf[i * 2 + 1] = sample;  // Right
            phase += phaseIncrement;
            if (phase >= SINE_TABLE_SIZE) phase -= SINE_TABLE_SIZE;
        }

        // Stereo: 4 bytes per sample frame (2 bytes L + 2 bytes R)
        i2s_write(I2S_NUM_0, buf, samplesToWrite * 4, &bytesWritten, portMAX_DELAY);
        samplesWritten += samplesToWrite;
    }

    Serial.printf("[TONE] Wrote %d samples, %d bytes\n", samplesWritten, samplesWritten * 4);

    // Allow last DMA buffers to transmit before flushing
    delay(50);

    // Flush DMA buffers with silence
    i2s_zero_dma_buffer(I2S_NUM_0);

    _ampEnable(false);
}

void Tones::_queueTone(uint32_t freqHz, uint32_t durationMs) {
    if (!_audioReady || !_toneQueue) return;
    ToneCommand cmd = {freqHz, durationMs};
    xQueueSend(_toneQueue, &cmd, 0);  // non-blocking
}
#endif

void Tones::update() {
    unsigned long now = millis();

#ifdef WOKWI_SIM
    // Check if current tone has finished
    if (_wokwiTonePlaying && now >= _wokwiToneEndMs) {
        noTone(BUZZER_SIM);
        _wokwiTonePlaying = false;
    }

    // Process scheduled events
    for (int i = 0; i < _count; i++) {
        if (!_ev[i].played && now >= _ev[i].atMs) {
            _ev[i].played = true;
            // Only play if not already playing
            if (!_wokwiTonePlaying) {
                tone(BUZZER_SIM, _ev[i].freqHz);
                _wokwiToneEndMs = now + _ev[i].durationMs;
                _wokwiTonePlaying = true;
            }
        }
    }
#endif

#ifdef WAVESHARE_HW
    for (int i = 0; i < _count; i++) {
        if (!_ev[i].played && now >= _ev[i].atMs) {
            _ev[i].played = true;
            _queueTone(_ev[i].freqHz, _ev[i].durationMs);
        }
    }
#endif
}

void Tones::playWindowOpen() {
#ifdef WOKWI_SIM
    tone(BUZZER_SIM, 1200, 1200);
#endif
#ifdef WAVESHARE_HW
    if (!_audioReady) return;
    _queueTone(1200, 1200);  // sustained high note — window is open
#endif
}

void Tones::testTone() {
#ifdef WOKWI_SIM
    Serial.println("[TONE] Playing startup beep (Wokwi)");
    tone(BUZZER_SIM, 880, 150);
#endif

#ifdef WAVESHARE_HW
    Serial.println("[TONE] Playing startup beep");
    if (_audioReady && _sineBuf) {
        _queueTone(880, 150);  // Short beep at A5
    }
#endif
}

void Tones::playAlert(int t) {
#ifdef WOKWI_SIM
    switch (t) {
        case 30:
        case 20:
        case 10: case 9: case 8: case 7: case 6: case 5: case 4: case 3: case 2: case 1:
            tone(BUZZER_SIM, 880, 200);
            break;
        case 15:
            tone(BUZZER_SIM, 880, 100);
            break;
        case 0:
            tone(BUZZER_SIM, 440, 400);
            break;
        default:
            break;
    }
#endif

#ifdef WAVESHARE_HW
    if (!_audioReady) return;

    switch (t) {
        case 30:
        case 20:
        case 15:
        case 10: case 9: case 8: case 7: case 6: case 5: case 4: case 3: case 2: case 1:
            // Single beep for all countdown alerts
            _queueTone(880, 300);
            break;
        case 0:
            // Time's up - longer descending tone
            _queueTone(880, 400);
            _queueTone(440, 600);
            break;
        default:
            break;
    }
#endif
}
