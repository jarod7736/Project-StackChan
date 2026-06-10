#include "app/ControlBridge.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "face/Face.h"
#include "motion/MotionDirector.h"
#include "hal/Servos.h"
#include "hal/AudioPlayer.h"
#include "state_machine.h"

namespace stkchan {

ControlBridge controlBridge;

namespace {
QueueHandle_t g_queue = nullptr;
constexpr int kQueueDepth = 8;
}  // namespace

void ControlBridge::begin() {
    if (g_queue) return;
    g_queue = xQueueCreate(kQueueDepth, sizeof(ControlCommand));
}

bool ControlBridge::push(const ControlCommand& c) {
    if (!g_queue) return false;
    // 0-tick timeout: never block the web-server task; drop if full.
    return xQueueSend(g_queue, &c, 0) == pdTRUE;
}

bool ControlBridge::pushExpression(const char* tag) {
    ControlCommand c{};
    c.type = CtrlCmd::EXPRESSION;
    strncpy(c.text, tag ? tag : "neutral", sizeof(c.text) - 1);
    return push(c);
}

bool ControlBridge::pushServo(int yawDeg, int pitchDeg) {
    ControlCommand c{};
    c.type = CtrlCmd::SERVO;
    c.a = yawDeg;
    c.b = pitchDeg;
    return push(c);
}

bool ControlBridge::pushVolume(int pct) {
    ControlCommand c{};
    c.type = CtrlCmd::VOLUME;
    c.a = pct;
    return push(c);
}

bool ControlBridge::pushSay(const char* text) {
    ControlCommand c{};
    c.type = CtrlCmd::SAY;
    strncpy(c.text, text ? text : "", sizeof(c.text) - 1);
    return push(c);
}

void ControlBridge::tick() {
    if (!g_queue) return;
    ControlCommand c;
    // Drain everything queued this loop. Each dispatch runs on the main
    // task, so face/servo/audio/FSM access is safe.
    while (xQueueReceive(g_queue, &c, 0) == pdTRUE) {
        switch (c.type) {
            case CtrlCmd::EXPRESSION: {
                std::string tag(c.text);
                face.setExpression(tag);
                motion.onExpressionChange(tag);
                break;
            }
            case CtrlCmd::SERVO: {
                // Smooth move to the requested pose. Servos clamp internally
                // to their mechanical limits. Pause idle so saccades/nods
                // don't restart the ease under a streamed slider; the FSM
                // resumes idle after the next voice turn.
                motion.pauseIdle();
                servos.easeYawTo(c.a, 400);
                servos.easePitchTo(c.b, 400);
                break;
            }
            case CtrlCmd::VOLUME: {
                audio.setVolume(c.a);  // applies live + persists to NVS
                break;
            }
            case CtrlCmd::SAY: {
                // Reuse the FSM speak path; ignored if a voice turn is busy.
                if (!requestExternalSpeak(String(c.text), "happy")) {
                    Serial.println("[ControlBridge] say ignored (FSM busy)");
                }
                break;
            }
        }
    }
}

}  // namespace stkchan
