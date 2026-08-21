#pragma once
#include <cstdint>
#include "Types.h"

namespace airalert {

// SPEC 15. Name lives in the catalogue/UI layer, not in core matching.
struct Location {
    uint16_t uid = 0;
    LocationType type = LocationType::Unknown;
    uint16_t oblastUid = 0; // 0 = none/unknown
    uint16_t raionUid = 0;  // 0 = none/unknown
};

// Catalogue lookup used to enrich incoming alerts with hierarchy uids
// (the API itself carries only location_oblast_uid). SPEC 13, 16.
class ILocationCatalog {
public:
    virtual ~ILocationCatalog() = default;
    // Returns true and fills `out` if uid is known.
    virtual bool findByUid(uint16_t uid, Location& out) const = 0;
};

} // namespace airalert
