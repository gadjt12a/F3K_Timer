#pragma once
#include <Arduino.h>
#include "config.h"

#ifdef WAVESHARE_HW
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

struct ToneCommand {
    uint32_t freqHz;
    uint32_t durationMs;
};
#endif

class Tones {
public:
    void begin();
    void update();
    void playAlert(int timeRemaining);
    void silence();
    void testTone();

private:
    struct Event {
        unsigned long atMs;
        uint32_t      freqHz;
        uint32_t      durationMs;
        bool          played;
    };

    static const int QUEUE = 8;
    Event _ev[QUEUE];
    int   _count = 0;

    void _add(unsigned long atMs, uint32_t freq, uint32_t durMs);
    void _clear();

#ifdef WAVESHARE_HW
    bool _audioReady = false;
    bool _ampOn = false;

    static QueueHandle_t _toneQueue;
    static TaskHandle_t  _toneTask;
    static int16_t*      _sineBuf;
    static bool          _taskAmpOn;

    static void _toneTaskFunc(void* param);
    void _playToneBlocking(uint32_t freqHz, uint32_t durationMs);
    void _queueTone(uint32_t freqHz, uint32_t durationMs);
    static void _ampEnable(bool on);
#endif
};
