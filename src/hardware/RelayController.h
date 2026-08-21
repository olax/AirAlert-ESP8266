#pragma once
// Sole owner of the relay GPIO (SPEC 38-42, 172). Everything routes through
// RelayGuard, so no caller can hold the relay ON past the safety limit.
#include <Arduino.h>
#include "airalert/RelayGuard.h"

class RelayController {
public:
    void begin(uint8_t pin, bool activeHigh, uint32_t maxOnMs) {
        pin_ = pin;
        activeHigh_ = activeHigh;
        guard_.setLimit(maxOnMs);
        writeOff();
        pinMode(pin_, OUTPUT);
        writeOff();
    }

    // Feed the desired state every loop; the guard decides what the GPIO gets.
    void tick(bool requested, uint32_t nowMs) {
        apply(guard_.tick(requested, nowMs));
    }

    void forceOff() { // SPEC 40
        guard_.tick(false, 0); // release guard state
        apply(false);
    }

    bool isOn() const { return on_; }
    bool safetyTripped() const { return guard_.tripped(); }

private:
    void apply(bool on) {
        if (on == on_) return;
        on_ = on;
        digitalWrite(pin_, on == activeHigh_ ? HIGH : LOW);
        // commissioning aid (SPEC 141): every physical transition is visible
        Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
    }
    void writeOff() { digitalWrite(pin_, activeHigh_ ? LOW : HIGH); }

    airalert::RelayGuard guard_;
    uint8_t pin_ = 255;
    bool activeHigh_ = true;
    bool on_ = false;
};
