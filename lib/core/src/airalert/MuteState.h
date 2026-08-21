#pragma once
#include <cstdint>
#include "Types.h"
#include "TimeUtils.h"

namespace airalert {

// SPEC 50-53. Mute silences the siren, never the alert state (Invariant 4 is
// enforced by the caller forcing relay OFF on mute()).
class MuteState {
public:
    enum class Scope : uint8_t { None, CurrentPattern, UntilClear, Snooze };

    struct Config {
        uint32_t snoozeMs = 15u * 60u * 1000u; // SPEC 52
        bool muteAllAlertTypes = false;        // SPEC 53
    };

    MuteState();
    explicit MuteState(const Config& cfg);
    void setConfig(const Config& cfg) { cfg_ = cfg; }

    // activeTypesMask: bit per AlertType currently active — remembered so a NEW
    // type may override the mute (SPEC 53).
    void mute(Scope scope, uint32_t nowMs, uint8_t activeTypesMask) {
        scope_ = scope;
        since_ = nowMs;
        mutedTypesMask_ = activeTypesMask;
    }

    void unmute() { scope_ = Scope::None; mutedTypesMask_ = 0; }

    // All alerts ended -> UntilClear mute expires (SPEC 52).
    void onAllClear() {
        if (scope_ == Scope::UntilClear || scope_ == Scope::CurrentPattern) unmute();
    }

    // Current pattern finished -> CurrentPattern mute expires.
    void onPatternFinished() {
        if (scope_ == Scope::CurrentPattern) unmute();
    }

    // Should this signal be silenced right now?
    bool shouldSilence(AlertType type, uint32_t nowMs) {
        if (scope_ == Scope::None) return false;
        if (scope_ == Scope::Snooze && intervalPassed(nowMs, since_, cfg_.snoozeMs)) {
            unmute();
            return false;
        }
        if (cfg_.muteAllAlertTypes) return true;
        const uint8_t bit = 1u << static_cast<uint8_t>(type);
        return (mutedTypesMask_ & bit) != 0; // new type overrides mute
    }

    bool muted() const { return scope_ != Scope::None; }
    Scope scope() const { return scope_; }

private:
    Config cfg_;
    Scope scope_ = Scope::None;
    uint32_t since_ = 0;
    uint8_t mutedTypesMask_ = 0;
};

inline MuteState::MuteState() = default;
inline MuteState::MuteState(const Config& cfg) : cfg_(cfg) {}

} // namespace airalert
