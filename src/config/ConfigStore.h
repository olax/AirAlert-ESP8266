#pragma once
// LittleFS-backed config persistence with atomic replace (SPEC 87, 89-90).
#include <Arduino.h>
#include <LittleFS.h>
#include "airalert/Config.h"

class ConfigStore {
public:
    static constexpr const char* kPath = "/config.json";
    static constexpr const char* kTmpPath = "/config.new";

    // Missing/corrupt file -> defaults (never brick on bad config, SPEC 128).
    bool load(airalert::AppConfig& c) {
        File f = LittleFS.open(kPath, "r");
        if (!f) return false;
        JsonDocument d;
        const bool ok = deserializeJson(d, f) == DeserializationError::Ok;
        f.close();
        if (!ok) return false;
        airalert::configFromJson(d.as<JsonVariantConst>(), c);
        c.schemaVersion = airalert::kConfigSchema; // migrated on load (SPEC 90)
        return airalert::validateConfig(c) == airalert::ConfigError::None;
    }

    // write tmp -> close -> re-read/validate -> rename (SPEC 89)
    bool save(const airalert::AppConfig& c) {
        if (airalert::validateConfig(c) != airalert::ConfigError::None) return false;
        {
            File f = LittleFS.open(kTmpPath, "w");
            if (!f) return false;
            JsonDocument d;
            airalert::configToJson(c, d);
            serializeJson(d, f);
            f.close();
        }
        {
            File f = LittleFS.open(kTmpPath, "r");
            JsonDocument check;
            const bool ok = f && deserializeJson(check, f) == DeserializationError::Ok;
            if (f) f.close();
            if (!ok) { LittleFS.remove(kTmpPath); return false; }
        }
        LittleFS.remove(kPath);
        return LittleFS.rename(kTmpPath, kPath);
    }
};
