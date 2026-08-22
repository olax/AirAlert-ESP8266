#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace atomic_json {

inline bool readOne(const char* path, JsonDocument& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    const bool ok = deserializeJson(out, f) == DeserializationError::Ok;
    f.close();
    return ok;
}

inline bool readWithBackup(const char* path, const char* backupPath,
                           JsonDocument& out) {
    if (readOne(path, out)) return true;
    out.clear();
    return readOne(backupPath, out);
}

inline bool write(const char* path, const char* temporaryPath,
                  const char* backupPath, const JsonDocument& document) {
    LittleFS.remove(temporaryPath);
    File f = LittleFS.open(temporaryPath, "w");
    if (!f) return false;
    const size_t expected = measureJson(document);
    const size_t written = serializeJson(document, f);
    f.flush();
    const bool writeOk = written == expected && f.getWriteError() == 0;
    f.close();
    if (!writeOk) {
        LittleFS.remove(temporaryPath);
        return false;
    }

    JsonDocument check;
    if (!readOne(temporaryPath, check)) {
        LittleFS.remove(temporaryPath);
        return false;
    }

    LittleFS.remove(backupPath);
    const bool hadCurrent = LittleFS.exists(path);
    if (hadCurrent && !LittleFS.rename(path, backupPath)) {
        LittleFS.remove(temporaryPath);
        return false;
    }
    if (!LittleFS.rename(temporaryPath, path)) {
        if (hadCurrent) LittleFS.rename(backupPath, path);
        return false;
    }
    LittleFS.remove(backupPath);
    return true;
}

} // namespace atomic_json
