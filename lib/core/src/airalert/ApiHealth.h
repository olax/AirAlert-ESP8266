#pragma once
#include <cstdint>
#include "TimeUtils.h"

namespace airalert {

// Data-health tracking, separate from alert state (SPEC 30-31, Invariant 1).
class ApiHealth {
public:
    struct Config { uint32_t staleAfterMs = 60000; };

    ApiHealth();
    explicit ApiHealth(const Config& cfg);
    void setConfig(const Config& cfg) { cfg_ = cfg; }

    // 200 and 304 both count as contact (SPEC 8).
    void onContact(uint32_t nowMs) { online_ = true; hadContact_ = true; lastContact_ = nowMs; }
    void onFailure() { online_ = false; }

    bool online() const { return online_; }
    bool everContacted() const { return hadContact_; } // SPEC 166
    bool stale(uint32_t nowMs) const {
        return !hadContact_ || elapsedMs(nowMs, lastContact_) >= cfg_.staleAfterMs;
    }
    uint32_t sinceContactMs(uint32_t nowMs) const {
        return hadContact_ ? elapsedMs(nowMs, lastContact_) : UINT32_MAX;
    }

private:
    Config cfg_;
    uint32_t lastContact_ = 0;
    bool online_ = false;
    bool hadContact_ = false;
};

inline ApiHealth::ApiHealth() = default;
inline ApiHealth::ApiHealth(const Config& cfg) : cfg_(cfg) {}

} // namespace airalert
