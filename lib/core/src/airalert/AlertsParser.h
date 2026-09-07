#pragma once
#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>
#include "Alert.h"
#include "Iso8601.h"
#include "SnapshotBuilder.h"

namespace airalert {

enum class ParseError : uint8_t { None, JsonInvalid, NoAlertsArray };

struct ParseStats {
    uint16_t total = 0;   // active alerts seen in the response (all regions)
    uint16_t skipped = 0; // entries without a usable region id, or INFO messages
};

// ukrainealarm.com GET /api/v3/alerts:
//   [{regionId, regionType, activeAlerts: [{regionId, type, lastUpdate,
//      activeAlertLevels: [{alertLevel, reason, createdAt}]}]}, ...]
// A region entry lists its own alerts plus the ones inherited from parents.
// Filters keep only the fields core logic needs (SPEC 111-112).
inline void regionFilter(JsonObject r) {
    JsonObject a = r["activeAlerts"].add<JsonObject>();
    a["regionId"] = true;
    a["type"] = true;
    a["lastUpdate"] = true;
    JsonObject l = a["activeAlertLevels"].add<JsonObject>();
    l["alertLevel"] = true;
    l["createdAt"] = true;
}
// Whole response (native tests / small bodies).
inline void buildAlertsFilter(JsonDocument& f) { regionFilter(f.add<JsonObject>()); }
// One region entry (constant-memory stream parser on the device).
inline void buildAlertElementFilter(JsonDocument& f) { regionFilter(f.to<JsonObject>()); }

// API serves ids as strings ("16"); tolerate numbers too.
inline uint16_t uidFromJson(JsonVariantConst v) {
    if (v.is<unsigned>()) return static_cast<uint16_t>(v.as<unsigned>());
    const char* s = v.as<const char*>();
    if (!s) return 0;
    unsigned r = 0;
    while (*s >= '0' && *s <= '9') r = r * 10 + static_cast<unsigned>(*s++ - '0');
    return static_cast<uint16_t>(r);
}

// One region entry -> alerts into the builder. Unknown types never abort
// (Invariant 9). INFO entries are messages, not threats: skipped.
// Duplicates (a parent alert repeated in every child entry) are harmless -
// the builder only keeps max coverage / earliest start per type.
// One alert may carry both levels; red dominates, so one physical raid is
// ever only ONE type (yellow only while no red level is active on it).
inline void regionToAlerts(JsonVariantConst region, SnapshotBuilder& b, ParseStats& st) {
    for (JsonObjectConst o : region["activeAlerts"].as<JsonArrayConst>()) {
        ++st.total;
        const char* type = o["type"] | static_cast<const char*>(nullptr);
        Alert a;
        a.locationUid = uidFromJson(o["regionId"]);
        if (a.locationUid == 0 || (type && !strcmp(type, "INFO"))) { ++st.skipped; continue; }
        JsonArrayConst levels = o["activeAlertLevels"];
        if (levels.size() == 0) { // ungraded alert = red
            a.type = alertTypeFromApi(type, nullptr);
            a.startedAt = parseIso8601Utc(o["lastUpdate"] | static_cast<const char*>(nullptr));
            b.add(a);
            continue;
        }
        Alert red = a, yellow = a;
        bool hasRed = false, hasYellow = false;
        for (JsonObjectConst l : levels) {
            const int64_t ts = parseIso8601Utc(l["createdAt"] | static_cast<const char*>(nullptr));
            Alert& lvl = alertTypeFromApi(type, l["alertLevel"] | static_cast<const char*>(nullptr))
                             == AlertType::AirRaidYellow ? yellow : red;
            (&lvl == &yellow ? hasYellow : hasRed) = true;
            if (lvl.startedAt == 0 || (ts > 0 && ts < lvl.startedAt)) lvl.startedAt = ts;
        }
        if (hasRed) { red.type = alertTypeFromApi(type, nullptr); b.add(red); }
        else if (hasYellow) { yellow.type = AlertType::AirRaidYellow; b.add(yellow); }
    }
}

// Walk a parsed (filtered) document: the root must be the region array.
inline ParseError extractAlerts(JsonVariantConst root, SnapshotBuilder& b, ParseStats& st) {
    JsonArrayConst regions = root.as<JsonArrayConst>();
    if (regions.isNull()) return ParseError::NoAlertsArray; // SPEC 167
    b.reset();
    for (JsonVariantConst r : regions) regionToAlerts(r, b, st);
    return ParseError::None;
}

// Convenience for buffers (native tests; ESP path feeds a Stream instead).
inline ParseError parseAlertsJson(const char* json, size_t len,
                                  SnapshotBuilder& b, ParseStats& st) {
    JsonDocument filter;
    buildAlertsFilter(filter);
    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))
        != DeserializationError::Ok)
        return ParseError::JsonInvalid;
    return extractAlerts(doc, b, st);
}

} // namespace airalert
