#pragma once
#include <cstdint>
#include "TimeUtils.h"

namespace airalert {

// SPEC 44.
struct Pattern {
    bool enabled = true;
    uint32_t onMs = 0;
    uint32_t offMs = 0;
    uint8_t repeat = 0;
};

// Non-blocking single-pattern playback (SPEC 43). Pure logic, millis-driven.
class PatternScheduler {
public:
    void start(const Pattern& p, uint32_t nowMs) {
        pattern_ = p;
        cycle_ = 0;
        phaseStart_ = nowMs;
        running_ = p.enabled && p.repeat > 0 && p.onMs > 0;
        inOn_ = running_;
    }

    void stop() { running_ = false; inOn_ = false; }

    // Advance; returns desired output state (true = relay/LED ON).
    bool tick(uint32_t nowMs) {
        if (!running_) return false;
        // Phase boundaries advance by NOMINAL durations, not by tick time,
        // so a coarse tick interval cannot stretch the pattern.
        if (inOn_) {
            if (intervalPassed(nowMs, phaseStart_, pattern_.onMs)) {
                inOn_ = false;
                phaseStart_ += pattern_.onMs;
                if (++cycle_ >= pattern_.repeat) { running_ = false; return false; }
                if (pattern_.offMs == 0) inOn_ = true; // zero gap: chain into next ON
            }
        } else {
            if (intervalPassed(nowMs, phaseStart_, pattern_.offMs)) {
                inOn_ = true;
                phaseStart_ += pattern_.offMs;
            }
        }
        return running_ && inOn_;
    }

    bool running() const { return running_; }

private:
    Pattern pattern_{};
    uint32_t phaseStart_ = 0;
    uint8_t cycle_ = 0;
    bool running_ = false;
    bool inOn_ = false;
};

} // namespace airalert
