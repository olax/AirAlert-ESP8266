#include "AlertEngine.h"

namespace airalert {

namespace {
inline void push(EngineEvent* ev, size_t& n, size_t max, AlertEvent k, uint8_t typeIdx) {
    if (n < max) ev[n++] = {k, static_cast<AlertType>(typeIdx)};
}
} // namespace

bool AlertEngine::anyActive() const {
    for (const auto& t : types_)
        if (t.state == AlertState::Active) return true;
    return false;
}

bool AlertEngine::isHandover(const EngineEvent& ev, const EngineEvent* batch, size_t n,
                             bool anyType) const {
    const auto isAir = [](AlertType t) {
        return t == AlertType::AirRaid || t == AlertType::AirRaidYellow;
    };
    for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
        const AlertType other = static_cast<AlertType>(i);
        if (other == ev.type || !(anyType || (isAir(ev.type) && isAir(other)))) continue;
        const TypeStatus& ts = types_[i];
        const bool active = ts.state == AlertState::Active;
        if (ev.kind == AlertEvent::Ended && active) return true;
        if (ev.kind != AlertEvent::Started) continue;
        if (active && ts.endCount > 0) return true;
        for (size_t k = 0; k < n; ++k)
            if (batch[k].kind == AlertEvent::Ended && batch[k].type == other) return true;
    }
    return false;
}

void AlertEngine::reset() {
    for (auto& type : types_) type = TypeStatus{};
    synced_ = false;
}

size_t AlertEngine::applySnapshot(const Snapshot& snap, EngineEvent* events, size_t maxEvents) {
    size_t n = 0;
    synced_ = true;

    for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
        TypeStatus& ts = types_[i];
        const TypeInput& in = snap.types[i];

        // SPEC 20: with partialActive=false a partial-only alert counts as none.
        Coverage cov = in.coverage;
        if (!cfg_.partialActive && cov == Coverage::Partial) cov = Coverage::None;
        const bool present = cov != Coverage::None;

        switch (ts.state) {
            case AlertState::Unknown:
            case AlertState::Inactive:
                ts.endCount = 0;
                ts.state = AlertState::Inactive; // synced: Unknown resolves either way
                if (present) {
                    if (++ts.startCount >= cfg_.startConfirmations) {
                        ts.state = AlertState::Active;
                        ts.coverage = cov;
                        ts.startedAt = in.earliestStartedAt;
                        ts.locationCount = in.locationCount;
                        ts.startCount = 0;
                        push(events, n, maxEvents, AlertEvent::Started, i);
                    }
                } else {
                    ts.startCount = 0;
                }
                break;

            case AlertState::Active:
                if (present) {
                    ts.endCount = 0;
                    if (ts.coverage == Coverage::Partial && cov == Coverage::Full)
                        push(events, n, maxEvents, AlertEvent::Escalated, i);
                    else if (ts.coverage == Coverage::Full && cov == Coverage::Partial)
                        push(events, n, maxEvents, AlertEvent::CoverageReduced, i);
                    if (in.locationCount > ts.locationCount)
                        push(events, n, maxEvents, AlertEvent::LocationAdded, i);
                    ts.coverage = cov;
                    ts.locationCount = in.locationCount;
                    if (ts.startedAt == 0) ts.startedAt = in.earliestStartedAt;
                } else {
                    // SPEC 29: end needs confirmations; SPEC 21 is upheld because
                    // aggregation keeps the type present while ANY location remains.
                    if (++ts.endCount >= cfg_.endConfirmations) {
                        ts = TypeStatus{};
                        ts.state = AlertState::Inactive;
                        push(events, n, maxEvents, AlertEvent::Ended, i);
                    }
                }
                break;
        }
    }
    return n;
}

} // namespace airalert
