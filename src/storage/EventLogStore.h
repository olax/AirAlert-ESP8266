#pragma once
// Two-file circular event log (SPEC 93-96): /log/ev.0 + /log/ev.1,
// rotate when the active file exceeds kMaxFileBytes. ~2x16 KB worst case.
#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>
#include "airalert/EventLog.h"

class EventLogStore {
public:
    static constexpr size_t kMaxFileBytes = 16 * 1024;

    void begin() {
        healthy_ = LittleFS.exists("/log") || LittleFS.mkdir("/log");
        // continue writing whichever file is currently the smaller/newer one
        const size_t s0 = fileSize("/log/ev.0"), s1 = fileSize("/log/ev.1");
        active_ = (s1 > 0 && s1 < s0) ? 1 : (s0 >= kMaxFileBytes ? 1 : 0);
    }

    void log(airalert::LogEvent e, const char* detail = nullptr) {
        char buf[160];
        const time_t now = time(nullptr);
        const int64_t ts = now > 1600000000 ? now : 0; // 0 = clock not synced
        const size_t n = airalert::formatLogRecord(buf, sizeof buf, ts, e, detail);
        if (!n) return;
        const char* path = active_ ? "/log/ev.1" : "/log/ev.0";
        if (fileSize(path) + n > kMaxFileBytes) {
            active_ ^= 1;
            path = active_ ? "/log/ev.1" : "/log/ev.0";
            if (LittleFS.exists(path) && !LittleFS.remove(path)) {
                healthy_ = false;
                Serial.println("[LOG] rotation failed");
                return;
            }
        }
        File f = LittleFS.open(path, "a");
        if (!f) {
            healthy_ = false;
            Serial.println("[LOG] open failed");
            return;
        }
        const size_t written = f.write(reinterpret_cast<const uint8_t*>(buf), n);
        f.flush();
        const bool ok = written == n && f.getWriteError() == 0;
        f.close();
        healthy_ = ok;
        if (!ok) {
            Serial.println("[LOG] write failed");
            return;
        }
        Serial.printf("[LOG] %s%s%s\n", airalert::logEventName(e),
                      detail ? " " : "", detail ? detail : "");
    }

    bool healthy() const { return healthy_; }
    const char* activePath() const { return active_ ? "/log/ev.1" : "/log/ev.0"; }
    const char* olderPath() const { return active_ ? "/log/ev.0" : "/log/ev.1"; }

private:
    static size_t fileSize(const char* p) {
        File f = LittleFS.open(p, "r");
        if (!f) return 0;
        const size_t s = f.size();
        f.close();
        return s;
    }
    uint8_t active_ = 0;
    bool healthy_ = false;
};
