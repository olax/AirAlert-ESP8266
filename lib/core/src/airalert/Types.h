#pragma once
#include <cstdint>
#include <cstring>

namespace airalert {

// SPEC 11 + ukrainealarm.com alert levels. The graded scheme (yellow = drone
// threat, red = missile threat) exists only for AIR, so the yellow level is
// modeled as its own type: own profile, own engine slot, own events.
// ponytail: add a level axis if the API ever grades the other types.
enum class AlertType : uint8_t {
    AirRaid,            // AIR, red level or ungraded
    ArtilleryShelling,
    UrbanFights,
    Chemical,
    Nuclear,
    AirRaidYellow,      // AIR, yellow level
    Unknown
};
constexpr uint8_t kAlertTypeCount = 7; // including Unknown
// NotificationEngine::activeMask_ and MuteState::mutedTypesMask_ are uint8_t
// bitmasks indexed by AlertType: an 9th type would silently alias bit 0.
static_assert(kAlertTypeCount <= 8, "AlertType bitmasks are uint8_t");

// SPEC 12
enum class LocationType : uint8_t { Oblast, Raion, Hromada, City, Unknown };

// SPEC 19
enum class Coverage : uint8_t { None, Partial, Full };

// SPEC 23
enum class AlertState : uint8_t { Unknown, Inactive, Active };

inline AlertType alertTypeFromString(const char* s) {
    if (!s) return AlertType::Unknown;
    if (!strcmp(s, "air_raid")) return AlertType::AirRaid;
    if (!strcmp(s, "artillery_shelling")) return AlertType::ArtilleryShelling;
    if (!strcmp(s, "urban_fights")) return AlertType::UrbanFights;
    if (!strcmp(s, "chemical")) return AlertType::Chemical;
    if (!strcmp(s, "nuclear")) return AlertType::Nuclear;
    if (!strcmp(s, "air_raid_yellow")) return AlertType::AirRaidYellow;
    return AlertType::Unknown;
}

// ukrainealarm.com /api/v3 `type` + `alertLevel` -> AlertType. Anything that
// is not explicitly "Yellow" (Red, missing, a future level) counts as red:
// for a siren an unknown level must err on the loud side.
inline AlertType alertTypeFromApi(const char* type, const char* level) {
    if (!type) return AlertType::Unknown;
    if (!strcmp(type, "AIR"))
        return (level && !strcmp(level, "Yellow")) ? AlertType::AirRaidYellow
                                                    : AlertType::AirRaid;
    if (!strcmp(type, "ARTILLERY")) return AlertType::ArtilleryShelling;
    if (!strcmp(type, "URBAN_FIGHTS")) return AlertType::UrbanFights;
    if (!strcmp(type, "CHEMICAL")) return AlertType::Chemical;
    if (!strcmp(type, "NUCLEAR")) return AlertType::Nuclear;
    return AlertType::Unknown;
}

inline const char* alertTypeToString(AlertType t) {
    switch (t) {
        case AlertType::AirRaid: return "air_raid";
        case AlertType::ArtilleryShelling: return "artillery_shelling";
        case AlertType::UrbanFights: return "urban_fights";
        case AlertType::Chemical: return "chemical";
        case AlertType::Nuclear: return "nuclear";
        case AlertType::AirRaidYellow: return "air_raid_yellow";
        default: return "unknown";
    }
}

inline LocationType locationTypeFromString(const char* s) {
    if (!s) return LocationType::Unknown;
    if (!strcmp(s, "oblast")) return LocationType::Oblast;
    if (!strcmp(s, "raion")) return LocationType::Raion;
    if (!strcmp(s, "hromada")) return LocationType::Hromada;
    if (!strcmp(s, "city")) return LocationType::City;
    return LocationType::Unknown;
}

} // namespace airalert
