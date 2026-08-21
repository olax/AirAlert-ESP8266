#pragma once
#include <cstdint>
#include "TimeUtils.h"

namespace airalert {

// Software debounce + short/long press detection (SPEC 54, 117). Pure.
class Debouncer {
public:
    struct Config {
        uint32_t debounceMs = 40;
        uint32_t longPressMs = 2000;
    };
    enum class Event : uint8_t { None, Press, LongPress, Release };

    Debouncer();
    explicit Debouncer(const Config& cfg);

    // rawPressed: current (already polarity-corrected) input level.
    Event update(bool rawPressed, uint32_t nowMs) {
        if (rawPressed != lastRaw_) {
            lastRaw_ = rawPressed;
            rawChangeAt_ = nowMs;
        }
        if (rawPressed != stable_ && intervalPassed(nowMs, rawChangeAt_, cfg_.debounceMs)) {
            stable_ = rawPressed;
            if (stable_) {
                pressedAt_ = nowMs;
                longFired_ = false;
                return Event::Press;
            }
            return Event::Release;
        }
        if (stable_ && !longFired_ && intervalPassed(nowMs, pressedAt_, cfg_.longPressMs)) {
            longFired_ = true;
            return Event::LongPress;
        }
        return Event::None;
    }

    // On Release: was this a short press (long threshold never reached)?
    bool wasShortPress() const { return !longFired_; }
    bool pressed() const { return stable_; }

private:
    Config cfg_;
    bool lastRaw_ = false;
    bool stable_ = false;
    bool longFired_ = false;
    uint32_t rawChangeAt_ = 0;
    uint32_t pressedAt_ = 0;
};

inline Debouncer::Debouncer() = default;
inline Debouncer::Debouncer(const Config& cfg) : cfg_(cfg) {}

} // namespace airalert
