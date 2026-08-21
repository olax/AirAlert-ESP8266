#pragma once
#include <cstdint>
#include "Types.h"

namespace airalert {

// One active alert as parsed from /v1/alerts/active.json (SPEC 112, 169).
struct Alert {
    uint32_t id = 0;
    AlertType type = AlertType::Unknown;
    uint16_t locationUid = 0;
    uint16_t oblastUid = 0;   // from API location_oblast_uid; 0 if absent
    LocationType locationType = LocationType::Unknown;
    int64_t startedAt = 0;    // unix seconds, 0 if unknown
};

} // namespace airalert
