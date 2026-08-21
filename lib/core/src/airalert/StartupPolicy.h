#pragma once
#include <cstdint>
#include "AlertEngine.h"
#include "Types.h"

namespace airalert {

// Reboot-loop guard (SPEC 26-28): boot into an active alert plays a SHORT
// notification - but only once per alert set per cooldown window, so a
// crash-looping device cannot re-trigger the siren forever.
class StartupPolicy {
public:
    // FNV-1a over (type, startedAt) pairs of active types.
    static uint32_t fingerprint(const AlertEngine::Snapshot& s) {
        uint32_t h = 2166136261u;
        auto mix = [&h](uint32_t v) {
            for (int i = 0; i < 4; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 16777619u; }
        };
        for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
            const auto& t = s.types[i];
            if (t.coverage == Coverage::None) continue;
            mix(i);
            mix(static_cast<uint32_t>(t.earliestStartedAt));
            mix(static_cast<uint32_t>(t.earliestStartedAt >> 32));
        }
        return h;
    }

    // nowUtc: unix seconds (0 if NTP not synced yet -> always notify, but
    // then a crash loop before NTP cannot store a meaningful timestamp either;
    // cooldown still works via the stored fingerprint alone in that case).
    static bool shouldNotify(uint32_t fp, uint32_t storedFp, int64_t storedAtUtc,
                             int64_t nowUtc, uint16_t cooldownSec) {
        if (fp == 0x811C9DC5u) return false; // empty set: nothing to notify (FNV basis)
        if (fp != storedFp) return true;     // different alert set
        if (nowUtc == 0 || storedAtUtc == 0) return false; // same set, no clock: stay silent
        return (nowUtc - storedAtUtc) >= cooldownSec;
    }
};

} // namespace airalert
