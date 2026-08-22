#pragma once
#include <cstdint>
#include <cstddef>
#include "Types.h"

namespace airalert {

// Events for the notification layer (SPEC 34).
enum class AlertEvent : uint8_t {
    None,
    Started,          // ALERT_STARTED
    Escalated,        // Partial -> Full (SPEC 35)
    CoverageReduced,  // Full -> Partial, no signal by default (SPEC 36)
    LocationAdded,    // additional location joined an active type (SPEC 37)
    Ended             // ALERT_ENDED
};

struct EngineEvent {
    AlertEvent kind = AlertEvent::None;
    AlertType type = AlertType::Unknown;
};

// Per-threat-type state machine over matched snapshots (SPEC 21-37).
// Input: aggregated Coverage per type per snapshot; the engine applies
// start/end confirmations and emits transition events.
// Data staleness is deliberately NOT here: stale means "no new snapshots
// arrive" and the engine simply retains its last state (SPEC 30, Invariant 1).
class AlertEngine {
public:
    struct Config {
        uint8_t startConfirmations = 1;  // SPEC 29, Invariant 10
        uint8_t endConfirmations = 2;
        bool partialActive = true;       // SPEC 20
    };

    // Aggregated view of one snapshot after matching (SPEC 19, 21).
    struct TypeInput {
        Coverage coverage = Coverage::None;
        uint8_t locationCount = 0;   // matched selected locations, for LocationAdded
        int64_t earliestStartedAt = 0;
    };
    struct Snapshot {
        TypeInput types[kAlertTypeCount]; // indexed by AlertType
    };

    struct TypeStatus {
        AlertState state = AlertState::Unknown;
        Coverage coverage = Coverage::None;
        int64_t startedAt = 0;       // for duration = now - startedAt (SPEC 170)
        uint8_t startCount = 0;
        uint8_t endCount = 0;
        uint8_t locationCount = 0;
    };

    AlertEngine();
    explicit AlertEngine(const Config& cfg);

    void setConfig(const Config& cfg) { cfg_ = cfg; }

    // Apply one VALIDATED snapshot (SPEC 167-168). Returns number of events
    // written into `events` (up to maxEvents).
    size_t applySnapshot(const Snapshot& snap, EngineEvent* events, size_t maxEvents);
    void reset();

    const TypeStatus& status(AlertType t) const { return types_[static_cast<uint8_t>(t)]; }
    bool anyActive() const;
    bool synced() const { return synced_; } // false until first snapshot (SPEC 166)

private:
    Config cfg_;
    TypeStatus types_[kAlertTypeCount];
    bool synced_ = false;
};

inline AlertEngine::AlertEngine() = default;
inline AlertEngine::AlertEngine(const Config& cfg) : cfg_(cfg) {}

} // namespace airalert
