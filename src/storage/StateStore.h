#pragma once
// Minimal persisted runtime state (SPEC 28, 96, 169). Written only on change.
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

struct PersistedState {
    uint32_t alertFingerprint = 0;   // StartupPolicy::fingerprint of active set
    int64_t startupNotifAtUtc = 0;   // last startup notification (SPEC 28)
};

class StateStore {
public:
    static constexpr const char* kPath = "/state.json";

    bool load(PersistedState& s) {
        File f = LittleFS.open(kPath, "r");
        if (!f) return false;
        JsonDocument d;
        const bool ok = deserializeJson(d, f) == DeserializationError::Ok;
        f.close();
        if (!ok) return false;
        s.alertFingerprint = d["fp"] | 0u;
        s.startupNotifAtUtc = d["notif_at"] | 0ll;
        return true;
    }

    bool save(const PersistedState& s) {
        if (s.alertFingerprint == last_.alertFingerprint &&
            s.startupNotifAtUtc == last_.startupNotifAtUtc)
            return true; // no change, no flash wear (SPEC 96)
        File f = LittleFS.open(kPath, "w"); // tiny file: plain write is fine
        if (!f) return false;
        JsonDocument d;
        d["fp"] = s.alertFingerprint;
        d["notif_at"] = s.startupNotifAtUtc;
        serializeJson(d, f);
        f.close();
        last_ = s;
        return true;
    }

private:
    PersistedState last_;
};
