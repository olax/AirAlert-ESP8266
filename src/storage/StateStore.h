#pragma once
// Minimal persisted runtime state (SPEC 28, 96, 169). Written only on change.
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config/AtomicJsonFile.h"

struct PersistedState {
    uint32_t alertFingerprint = 0;   // StartupPolicy::fingerprint of active set
    int64_t startupNotifAtUtc = 0;   // last startup notification (SPEC 28)
};

class StateStore {
public:
    static constexpr const char* kPath = "/state.json";
    static constexpr const char* kTmpPath = "/state.new";
    static constexpr const char* kBackupPath = "/state.bak";

    bool load(PersistedState& s) {
        JsonDocument d;
        if (!atomic_json::readWithBackup(kPath, kBackupPath, d)) return false;
        s.alertFingerprint = d["fp"] | 0u;
        s.startupNotifAtUtc = d["notif_at"] | 0ll;
        last_ = s;
        return true;
    }

    bool save(const PersistedState& s) {
        if (s.alertFingerprint == last_.alertFingerprint &&
            s.startupNotifAtUtc == last_.startupNotifAtUtc)
            return true; // no change, no flash wear (SPEC 96)
        JsonDocument d;
        d["fp"] = s.alertFingerprint;
        d["notif_at"] = s.startupNotifAtUtc;
        if (!atomic_json::write(kPath, kTmpPath, kBackupPath, d)) return false;
        last_ = s;
        return true;
    }

private:
    PersistedState last_;
};
