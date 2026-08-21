#pragma once
#include <cstdint>
#include <cstring>

namespace airalert {

// SPEC 11
enum class AlertType : uint8_t {
    AirRaid,
    ArtilleryShelling,
    UrbanFights,
    Chemical,
    Nuclear,
    Unknown
};
constexpr uint8_t kAlertTypeCount = 6; // including Unknown

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
    return AlertType::Unknown;
}

inline const char* alertTypeToString(AlertType t) {
    switch (t) {
        case AlertType::AirRaid: return "air_raid";
        case AlertType::ArtilleryShelling: return "artillery_shelling";
        case AlertType::UrbanFights: return "urban_fights";
        case AlertType::Chemical: return "chemical";
        case AlertType::Nuclear: return "nuclear";
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
