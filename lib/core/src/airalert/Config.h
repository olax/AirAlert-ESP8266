#pragma once
#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>
#include "AlertProfiles.h"
#include "Location.h"
#include "SnapshotBuilder.h"
#include "Types.h"

namespace airalert {

constexpr uint8_t kConfigSchema = 1;

// Whole-app configuration (SPEC 121 defaults, 175 separation).
// Secrets live in /secrets.json, never here (SPEC 91, 176).
struct AppConfig {
    uint8_t schemaVersion = kConfigSchema;
    char deviceName[32] = "AirAlert";

    // alerts
    uint16_t pollIntervalSec = 15;   // min 10 (SPEC 7)
    uint16_t apiStaleAfterSec = 60;
    uint8_t startConfirmations = 1;
    uint8_t endConfirmations = 2;
    bool partialActive = true;
    bool partialSiren = true;
    bool partialLed = true;
    bool notifyEscalation = true;
    bool notifyAdditionalLocation = false;
    bool remindersWhenStale = false;

    // relay (SPEC 39, 41-42, 54)
    bool relayActiveHigh = true;
    uint32_t relayMaxOnMs = 30000;
    uint16_t relayTestMs = 600;

    // mute (SPEC 52-53)
    uint16_t snoozeMinutes = 15;
    bool muteAllAlertTypes = false;

    // reboot-loop guard (SPEC 28)
    uint16_t startupCooldownSec = 300;

    // indication (simplified two-channel scheme; rgb mode reserved for the
    // future WS2812/MCP23017 backend)
    bool alertIndicatorSteady = false;     // true when it drives a relay/lamp
    bool alertIndicatorActiveHigh = true;

    // selected locations (SPEC 21)
    Location selected[SnapshotBuilder::kMaxSelected];
    uint8_t selectedCount = 0;

    AlertProfile profiles[kAlertTypeCount];

    AppConfig() {
        for (uint8_t i = 0; i < kAlertTypeCount; ++i)
            profiles[i] = defaultProfile(static_cast<AlertType>(i));
        // dev default until the locations UI exists: м. Київ + Київська обл.
        selected[0] = {31, LocationType::City, 0, 0};
        selected[1] = {14, LocationType::Oblast, 0, 0};
        selectedCount = 2;
    }
};

enum class ConfigError : uint8_t {
    None, BadPollInterval, BadConfirmations, BadRelayLimit, BadTestMs,
    BadSnooze, TooManyLocations, BadPattern
};

// Hard limits (SPEC 42, 155): backend validation, never UI-only.
inline ConfigError validateConfig(const AppConfig& c) {
    if (c.pollIntervalSec < 10 || c.pollIntervalSec > 600) return ConfigError::BadPollInterval;
    if (c.startConfirmations < 1 || c.startConfirmations > 5 ||
        c.endConfirmations < 1 || c.endConfirmations > 10) return ConfigError::BadConfirmations;
    if (c.relayMaxOnMs < 1000 || c.relayMaxOnMs > 60000) return ConfigError::BadRelayLimit;
    if (c.relayTestMs < 100 || c.relayTestMs > 1000) return ConfigError::BadTestMs;
    if (c.snoozeMinutes < 1 || c.snoozeMinutes > 240) return ConfigError::BadSnooze;
    if (c.selectedCount > SnapshotBuilder::kMaxSelected) return ConfigError::TooManyLocations;
    for (const auto& p : c.profiles) {
        for (const Pattern* pt : {&p.start, &p.end, &p.reminder}) {
            if (pt->onMs > 60000 || pt->offMs > 60000 || pt->repeat > 20)
                return ConfigError::BadPattern;
        }
        if (p.reminderIntervalMs < 30000) return ConfigError::BadPattern;
    }
    return ConfigError::None;
}

// ---- JSON (de)serialization -------------------------------------------------

inline void patternToJson(JsonObject o, const Pattern& p) {
    o["enabled"] = p.enabled;
    o["on_ms"] = p.onMs;
    o["off_ms"] = p.offMs;
    o["repeat"] = p.repeat;
}
inline void patternFromJson(JsonVariantConst o, Pattern& p) {
    p.enabled = o["enabled"] | p.enabled;
    p.onMs = o["on_ms"] | p.onMs;
    p.offMs = o["off_ms"] | p.offMs;
    p.repeat = o["repeat"] | p.repeat;
}

inline const char* locationTypeToString(LocationType t) {
    switch (t) {
        case LocationType::Oblast: return "oblast";
        case LocationType::Raion: return "raion";
        case LocationType::Hromada: return "hromada";
        case LocationType::City: return "city";
        default: return "unknown";
    }
}

inline void configToJson(const AppConfig& c, JsonDocument& d) {
    d["schema"] = c.schemaVersion;
    d["device"]["name"] = c.deviceName;
    JsonObject a = d["alerts"].to<JsonObject>();
    a["poll_sec"] = c.pollIntervalSec;
    a["stale_sec"] = c.apiStaleAfterSec;
    a["start_conf"] = c.startConfirmations;
    a["end_conf"] = c.endConfirmations;
    a["partial_active"] = c.partialActive;
    a["partial_siren"] = c.partialSiren;
    a["partial_led"] = c.partialLed;
    a["notify_escalation"] = c.notifyEscalation;
    a["notify_additional_location"] = c.notifyAdditionalLocation;
    a["reminders_when_stale"] = c.remindersWhenStale;
    JsonObject r = d["relay"].to<JsonObject>();
    r["active_high"] = c.relayActiveHigh;
    r["max_on_ms"] = c.relayMaxOnMs;
    r["test_ms"] = c.relayTestMs;
    d["mute"]["snooze_min"] = c.snoozeMinutes;
    d["mute"]["all_types"] = c.muteAllAlertTypes;
    d["startup"]["cooldown_sec"] = c.startupCooldownSec;
    d["led"]["alert_steady"] = c.alertIndicatorSteady;
    d["led"]["alert_active_high"] = c.alertIndicatorActiveHigh;
    JsonArray locs = d["locations"].to<JsonArray>();
    for (uint8_t i = 0; i < c.selectedCount; ++i) {
        JsonObject o = locs.add<JsonObject>();
        o["uid"] = c.selected[i].uid;
        o["type"] = locationTypeToString(c.selected[i].type);
        if (c.selected[i].oblastUid) o["oblast_uid"] = c.selected[i].oblastUid;
        if (c.selected[i].raionUid) o["raion_uid"] = c.selected[i].raionUid;
    }
    JsonObject profs = d["profiles"].to<JsonObject>();
    for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
        const AlertProfile& p = c.profiles[i];
        JsonObject o = profs[alertTypeToString(static_cast<AlertType>(i))].to<JsonObject>();
        o["enabled"] = p.enabled;
        o["priority"] = p.priority;
        patternToJson(o["start"].to<JsonObject>(), p.start);
        patternToJson(o["end"].to<JsonObject>(), p.end);
        patternToJson(o["reminder"].to<JsonObject>(), p.reminder);
        o["reminder"]["interval_sec"] = p.reminderIntervalMs / 1000;
    }
}

// Missing fields keep defaults, so older configs load cleanly (SPEC 90).
inline void configFromJson(JsonVariantConst d, AppConfig& c) {
    c.schemaVersion = d["schema"] | c.schemaVersion;
    const char* name = d["device"]["name"] | static_cast<const char*>(nullptr);
    if (name) { strncpy(c.deviceName, name, sizeof c.deviceName - 1); c.deviceName[sizeof c.deviceName - 1] = 0; }
    JsonVariantConst a = d["alerts"];
    c.pollIntervalSec = a["poll_sec"] | c.pollIntervalSec;
    c.apiStaleAfterSec = a["stale_sec"] | c.apiStaleAfterSec;
    c.startConfirmations = a["start_conf"] | c.startConfirmations;
    c.endConfirmations = a["end_conf"] | c.endConfirmations;
    c.partialActive = a["partial_active"] | c.partialActive;
    c.partialSiren = a["partial_siren"] | c.partialSiren;
    c.partialLed = a["partial_led"] | c.partialLed;
    c.notifyEscalation = a["notify_escalation"] | c.notifyEscalation;
    c.notifyAdditionalLocation = a["notify_additional_location"] | c.notifyAdditionalLocation;
    c.remindersWhenStale = a["reminders_when_stale"] | c.remindersWhenStale;
    JsonVariantConst r = d["relay"];
    c.relayActiveHigh = r["active_high"] | c.relayActiveHigh;
    c.relayMaxOnMs = r["max_on_ms"] | c.relayMaxOnMs;
    c.relayTestMs = r["test_ms"] | c.relayTestMs;
    c.snoozeMinutes = d["mute"]["snooze_min"] | c.snoozeMinutes;
    c.muteAllAlertTypes = d["mute"]["all_types"] | c.muteAllAlertTypes;
    c.startupCooldownSec = d["startup"]["cooldown_sec"] | c.startupCooldownSec;
    c.alertIndicatorSteady = d["led"]["alert_steady"] | c.alertIndicatorSteady;
    c.alertIndicatorActiveHigh = d["led"]["alert_active_high"] | c.alertIndicatorActiveHigh;
    JsonArrayConst locs = d["locations"];
    if (!locs.isNull()) {
        c.selectedCount = 0;
        for (JsonObjectConst o : locs) {
            if (c.selectedCount >= SnapshotBuilder::kMaxSelected) break;
            Location& L = c.selected[c.selectedCount];
            L.uid = o["uid"] | 0;
            L.type = locationTypeFromString(o["type"] | static_cast<const char*>(nullptr));
            L.oblastUid = o["oblast_uid"] | 0;
            L.raionUid = o["raion_uid"] | 0;
            if (L.uid) ++c.selectedCount;
        }
    }
    JsonVariantConst profs = d["profiles"];
    if (!profs.isNull()) {
        for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
            JsonVariantConst o = profs[alertTypeToString(static_cast<AlertType>(i))];
            if (o.isNull()) continue;
            AlertProfile& p = c.profiles[i];
            p.enabled = o["enabled"] | p.enabled;
            p.priority = o["priority"] | p.priority;
            patternFromJson(o["start"], p.start);
            patternFromJson(o["end"], p.end);
            patternFromJson(o["reminder"], p.reminder);
            p.reminderIntervalMs = (o["reminder"]["interval_sec"] | (p.reminderIntervalMs / 1000)) * 1000UL;
        }
    }
}

} // namespace airalert
