#pragma once

// ControlBridge — thread-safe bridge from the async web server to the
// main-loop actuators (face, servos, audio, FSM).
//
// The ESPAsyncWebServer runs its handlers in a separate FreeRTOS task. The
// LVGL face, servos (I2C), audio, and FSM all live on the main loop() task
// and are NOT safe to touch from another task. So HTTP handlers only
// enqueue a Command here (push(), thread-safe via a FreeRTOS queue), and
// the main loop drains them via tick() and dispatches on its own task.

#include <Arduino.h>

namespace stkchan {

enum class CtrlCmd : uint8_t {
    EXPRESSION,   // text = tag (happy/sad/...)
    SERVO,        // a = yaw deg, b = pitch deg
    VOLUME,       // a = 0..100
    SAY,          // text = utterance
};

struct ControlCommand {
    CtrlCmd type;
    int     a = 0;
    int     b = 0;
    char    text[256];   // was 192 — MCP say allows 240 UTF-8 bytes + NUL headroom
    char    expr[12];    // expression tag for SAY (e.g. "happy"); empty = happy
};

class ControlBridge {
public:
    void begin();                       // create the queue
    bool push(const ControlCommand& c); // ANY task; false if queue full
    void tick();                        // main loop ONLY; drains + dispatches

    // Convenience push helpers (call from HTTP handlers).
    bool pushExpression(const char* tag);
    bool pushServo(int yawDeg, int pitchDeg);
    bool pushVolume(int pct);
    bool pushSay(const char* text, const char* expr = nullptr);
};

extern ControlBridge controlBridge;

}  // namespace stkchan
