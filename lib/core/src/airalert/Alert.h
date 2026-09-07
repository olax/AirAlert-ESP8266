#pragma once
#include <cstdint>
#include "Types.h"

namespace airalert {

// One active alert as parsed from ukrainealarm.com /api/v3/alerts (SPEC 112, 169).
struct Alert {
    AlertType type = AlertType::Unknown;
    uint16_t locationUid = 0; // ukrainealarm regionId; hierarchy comes from the catalogue
    int64_t startedAt = 0;    // unix seconds, 0 if unknown
};

} // namespace airalert
