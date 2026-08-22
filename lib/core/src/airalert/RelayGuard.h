#pragma once
#include <cstdint>
#include "TimeUtils.h"

namespace airalert {

// Invariant 2: relay cannot stay ON past the safety limit, no matter what the
// pattern layer asks for (SPEC 41). Sits between PatternEngine and the GPIO.
class RelayGuard {
public:
    explicit RelayGuard(uint32_t maxContinuousOnMs = 30000)
        : maxOnMs_(maxContinuousOnMs) {}

    void setLimit(uint32_t ms) { maxOnMs_ = ms; }
    void trip() { on_ = false; tripped_ = true; }

    // requested = what the pattern layer wants; returns what the relay gets.
    bool tick(bool requested, uint32_t nowMs) {
        if (!requested) { on_ = false; tripped_ = false; return false; }
        if (tripped_) return false; // stays off until released (request drops)
        if (!on_) { on_ = true; onSince_ = nowMs; }
        if (elapsedMs(nowMs, onSince_) >= maxOnMs_) {
            on_ = false;
            tripped_ = true;
            return false;
        }
        return true;
    }

    bool tripped() const { return tripped_; }

private:
    uint32_t maxOnMs_;
    uint32_t onSince_ = 0;
    bool on_ = false;
    bool tripped_ = false;
};

} // namespace airalert
