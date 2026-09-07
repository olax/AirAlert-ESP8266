#pragma once
// LittleFS-backed config persistence with atomic replace (SPEC 87, 89-90).
#include <Arduino.h>
#include <LittleFS.h>
#include "airalert/Config.h"
#include "AtomicJsonFile.h"

class ConfigStore {
public:
    static constexpr const char* kPath = "/config.json";
    static constexpr const char* kTmpPath = "/config.new";
    static constexpr const char* kBackupPath = "/config.bak";

    // Missing/corrupt file -> defaults (never brick on bad config, SPEC 128).
    bool load(airalert::AppConfig& c) {
        JsonDocument d;
        if (!atomic_json::readWithBackup(kPath, kBackupPath, d)) return false;
        airalert::AppConfig next;
        if (airalert::configFromJson(d.as<JsonVariantConst>(), next) !=
            airalert::ConfigError::None)
            return false;
        c = next;
        return true;
    }

    // write tmp -> close -> re-read/validate -> rename (SPEC 89)
    bool save(const airalert::AppConfig& c) {
        if (airalert::validateConfig(c) != airalert::ConfigError::None) return false;
        JsonDocument d;
        airalert::configToJson(c, d);
        return atomic_json::write(kPath, kTmpPath, kBackupPath, d);
    }
};
