#pragma once
// Physical MUTE/TEST buttons, INPUT_PULLUP wiring: pressed = LOW (SPEC 61, 117).
#include <Arduino.h>
#include "airalert/Debouncer.h"

class ButtonController {
public:
    using Event = airalert::Debouncer::Event;

    void begin(uint8_t pin) {
        pin_ = pin;
        pinMode(pin_, INPUT_PULLUP);
    }

    Event tick(uint32_t nowMs) {
        return deb_.update(digitalRead(pin_) == LOW, nowMs);
    }

    bool wasShortPress() const { return deb_.wasShortPress(); }

private:
    airalert::Debouncer deb_;
    uint8_t pin_ = 255;
};
