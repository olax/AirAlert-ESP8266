#pragma once
// Sole owner of the relay GPIO (SPEC 38-42, 172). Everything routes through
// RelayGuard, so no caller can hold the relay ON past the safety limit.
#include <Arduino.h>
#include <Ticker.h>
#include "airalert/RelayGuard.h"

class RelayController {
public:
    void begin(uint8_t pin, bool activeHigh, uint32_t maxOnMs) {
        safetyTimer_.detach();
        pin_ = pin;
        activeHigh_ = activeHigh;
        maxOnMs_ = maxOnMs;
        asyncTrip_ = false;
        asyncOffNeedsLog_ = false;
        tripEvent_ = false;
        guard_.tick(false, 0);
        guard_.setLimit(maxOnMs);
        writeOff();
        pinMode(pin_, OUTPUT);
        writeOff();
    }

    // Feed the desired state every loop; the guard decides what the GPIO gets.
    void tick(bool requested, uint32_t nowMs) {
        const bool trippedBefore = guard_.tripped();
        if (asyncTrip_) guard_.trip();
        apply(guard_.tick(requested, nowMs));
        if (!trippedBefore && guard_.tripped()) tripEvent_ = true;
        if (!requested) asyncTrip_ = false;
        if (asyncOffNeedsLog_) {
            asyncOffNeedsLog_ = false;
            Serial.println("[RELAY] OFF (async safety limit)");
        }
    }

    void forceOff() { // SPEC 40
        safetyTimer_.detach();
        asyncTrip_ = false;
        asyncOffNeedsLog_ = false;
        tripEvent_ = false;
        guard_.tick(false, 0); // release guard state
        apply(false);
    }

    bool isOn() const { return on_; }
    bool safetyTripped() const { return asyncTrip_ || guard_.tripped(); }
    bool consumeSafetyTrip() {
        const bool event = tripEvent_;
        tripEvent_ = false;
        return event;
    }

private:
    static void safetyTimeout(RelayController* self) { self->emergencyOff(); }

    // SYS-context deadline: it remains effective while loop() is blocked.
    void emergencyOff() {
        digitalWrite(pin_, activeHigh_ ? LOW : HIGH);
        on_ = false;
        asyncTrip_ = true;
        asyncOffNeedsLog_ = true;
        tripEvent_ = true;
    }

    void apply(bool on) {
        if (on == on_) return;
        on_ = on;
        digitalWrite(pin_, on == activeHigh_ ? HIGH : LOW);
        if (on)
            safetyTimer_.once_ms(maxOnMs_, safetyTimeout, this);
        else
            safetyTimer_.detach();
        // commissioning aid (SPEC 141): every physical transition is visible
        Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
    }
    void writeOff() { digitalWrite(pin_, activeHigh_ ? LOW : HIGH); }

    airalert::RelayGuard guard_;
    Ticker safetyTimer_;
    uint8_t pin_ = 255;
    uint32_t maxOnMs_ = 30000;
    bool activeHigh_ = true;
    volatile bool on_ = false;
    volatile bool asyncTrip_ = false;
    volatile bool asyncOffNeedsLog_ = false;
    volatile bool tripEvent_ = false;
};
