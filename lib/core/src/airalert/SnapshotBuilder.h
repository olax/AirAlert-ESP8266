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
        for (auto& row : perLoc_)
            for (auto& c : row) c = PerLocCell{};
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
            // per-location detail for the dashboard (SPEC 71)
            PerLocCell& cell = perLoc_[i][t];
            if (cov > cell.coverage) cell.coverage = cov;
            const uint32_t ts = static_cast<uint32_t>(a.startedAt);
            if (ts > 0 && (cell.startedAt == 0 || ts < cell.startedAt))
                cell.startedAt = ts;
        }
        ti.locationCount = static_cast<uint8_t>(__builtin_popcount(matchedMask_[t]));
    }

    const AlertEngine::Snapshot& snapshot() const { return snap_; }

    // Per-location threat detail for the dashboard (SPEC 71):
    // what covers selected location i for type t, and since when.
    struct PerLocCell {
        uint32_t startedAt = 0; // unix seconds; uint32 is fine until 2106
        Coverage coverage = Coverage::None;
    };
    const PerLocCell& locCell(size_t selIdx, AlertType t) const {
        return perLoc_[selIdx][static_cast<uint8_t>(t)];
    }
    uint16_t selectedUid(size_t i) const { return selected_[i].uid; }

private:
    AlertEngine::Snapshot snap_{};
    uint16_t matchedMask_[kAlertTypeCount] = {};
    PerLocCell perLoc_[kMaxSelected][kAlertTypeCount];
    Location selected_[kMaxSelected];
    size_t selectedCount_ = 0;
    LocationMatcher matcher_{nullptr};
};

} // namespace airalert
