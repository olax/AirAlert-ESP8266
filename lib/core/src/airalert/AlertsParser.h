#pragma once
#include <cstdint>
#include <ArduinoJson.h>
#include "Alert.h"
#include "Iso8601.h"
#include "SnapshotBuilder.h"

namespace airalert {

enum class ParseError : uint8_t { None, JsonInvalid, NoAlertsArray };

struct ParseStats {
    uint16_t total = 0;   // alerts seen in the response
    uint16_t skipped = 0; // entries without a usable location uid
};

// Filter: keep only the fields core logic needs (SPEC 111-112).
inline void buildAlertsFilter(JsonDocument& f) {
    JsonObject a = f["alerts"].add<JsonObject>();
    a["id"] = true;
    a["alert_type"] = true;
    a["location_type"] = true;
    a["location_uid"] = true;
    a["location_oblast_uid"] = true;
    a["started_at"] = true;
    a["calculated"] = true;
}

// API serves uids as strings ("16"); tolerate numbers too.
inline uint16_t uidFromJson(JsonVariantConst v) {
    if (v.is<unsigned>()) return static_cast<uint16_t>(v.as<unsigned>());
    const char* s = v.as<const char*>();
    if (!s) return 0;
    unsigned r = 0;
    while (*s >= '0' && *s <= '9') r = r * 10 + static_cast<unsigned>(*s++ - '0');
    return static_cast<uint16_t>(r);
}

// Element-level filter: same fields, for one alert object (used by the
// constant-memory stream parser on the device).
inline void buildAlertElementFilter(JsonDocument& f) {
    f["id"] = true;
    f["alert_type"] = true;
    f["location_type"] = true;
    f["location_uid"] = true;
    f["location_oblast_uid"] = true;
    f["started_at"] = true;
    f["calculated"] = true;
}

// One alert object -> Alert. Unknown types never abort (Invariant 9).
inline Alert alertFromJson(JsonVariantConst o) {
    Alert a;
    a.id = o["id"] | 0u;
    a.type = alertTypeFromString(o["alert_type"] | static_cast<const char*>(nullptr));
    a.locationType = locationTypeFromString(o["location_type"] | static_cast<const char*>(nullptr));
    a.locationUid = uidFromJson(o["location_uid"]);
    a.oblastUid = uidFromJson(o["location_oblast_uid"]);
    a.startedAt = parseIso8601Utc(o["started_at"] | static_cast<const char*>(nullptr));
    return a;
}

// Walk a parsed (filtered) document. Unknown types/locations never abort:
// they become AlertType::Unknown / skipped entries (Invariant 9).
inline ParseError extractAlerts(JsonVariantConst root, SnapshotBuilder& b, ParseStats& st) {
    JsonArrayConst arr = root["alerts"];
    if (arr.isNull()) return ParseError::NoAlertsArray; // SPEC 167
    b.reset();
    for (JsonObjectConst o : arr) {
        ++st.total;
        Alert a = alertFromJson(o);
        if (a.locationUid == 0) { ++st.skipped; continue; }
        b.add(a);
    }
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
