#pragma once
// Built-in LED (GPIO2, inverted) as the interim ALERT indicator.
// ponytail: single on-board LED until MCP23017 + 2x RGB arrive (SPEC 56-60);
// SYSTEM states go to serial for now. Same semantic API the RGB backend
// will implement, so Phase 10 swaps the backend, not the callers.
#include <Arduino.h>

class StatusLed {
public:
    enum class Mode : uint8_t {
        Off,        // no alert
        AlertFull,  // solid ON
        AlertPartial, // slow blink 1000/1000
        Muted,      // short flash, long pause (SPEC 51)
        SirenOn     // mirrors relay: fast blink (safe test load, SPEC 199)
    };

    void begin(uint8_t pin, bool inverted) {
        pin_ = pin;
        inverted_ = inverted;
        pinMode(pin_, OUTPUT);
        write(false);
    }

    void setMode(Mode m) { mode_ = m; }

    void tick(uint32_t nowMs) {
        bool on = false;
        switch (mode_) {
            case Mode::Off: on = false; break;
            case Mode::AlertFull: on = true; break;
            case Mode::AlertPartial: on = (nowMs / 1000) % 2 == 0; break;
            case Mode::Muted: on = (nowMs % 3000) < 150; break; // SPEC 51
            case Mode::SirenOn: on = (nowMs / 125) % 2 == 0; break;
        }
        write(on);
    }

private:
    void write(bool on) {
        if (on == cur_) return;
        cur_ = on;
        digitalWrite(pin_, on != inverted_ ? HIGH : LOW);
    }
    uint8_t pin_ = 2;
    bool inverted_ = true;
    bool cur_ = false;
    Mode mode_ = Mode::Off;
};
