#pragma once
#include <cstdint>
#include <cstddef>
#include "Alert.h"
#include "AlertEngine.h"
#include "Location.h"
#include "LocationMatcher.h"

namespace airalert {

// Streams alerts one-by-one into an aggregated AlertEngine::Snapshot,
// so the full API response never needs to sit in RAM (SPEC 111, 113).
class SnapshotBuilder {
public:
    static constexpr size_t kMaxSelected = 16;

    void setCatalog(const ILocationCatalog* cat) { matcher_ = LocationMatcher(cat); }

    bool setSelected(const Location* sel, size_t n) {
        if (n > kMaxSelected) return false;
        for (size_t i = 0; i < n; ++i) selected_[i] = sel[i];
        selectedCount_ = n;
        return true;
    }
    size_t selectedCount() const { return selectedCount_; }

    void reset() {
        snap_ = AlertEngine::Snapshot{};
        for (auto& m : matchedMask_) m = 0;
    }

    void add(const Alert& a) {
        const uint8_t t = static_cast<uint8_t>(a.type);
        auto& ti = snap_.types[t];
        for (size_t i = 0; i < selectedCount_; ++i) {
            const Coverage cov = matcher_.match(selected_[i], a);
            if (cov == Coverage::None) continue;
            if (cov > ti.coverage) ti.coverage = cov; // Full > Partial (SPEC 19)
            matchedMask_[t] |= static_cast<uint16_t>(1u << i);
            if (a.startedAt > 0 &&
                (ti.earliestStartedAt == 0 || a.startedAt < ti.earliestStartedAt))
                ti.earliestStartedAt = a.startedAt;
        }
        ti.locationCount = static_cast<uint8_t>(__builtin_popcount(matchedMask_[t]));
    }

    const AlertEngine::Snapshot& snapshot() const { return snap_; }

private:
    AlertEngine::Snapshot snap_{};
    uint16_t matchedMask_[kAlertTypeCount] = {};
    Location selected_[kMaxSelected];
    size_t selectedCount_ = 0;
    LocationMatcher matcher_{nullptr};
};

} // namespace airalert
