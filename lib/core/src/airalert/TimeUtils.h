#pragma once
#include <cstdint>

namespace airalert {

// millis() wraparound-safe elapsed time (SPEC 137).
// Unsigned subtraction is well-defined across the 2^32 wrap.
inline uint32_t elapsedMs(uint32_t now, uint32_t since) { return now - since; }

inline bool intervalPassed(uint32_t now, uint32_t since, uint32_t intervalMs) {
    return elapsedMs(now, since) >= intervalMs;
}

} // namespace airalert
