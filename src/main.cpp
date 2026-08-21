// AirAlert-ESP8266 — Phase 3: live API polling, serial/debug state only.
// No relay activation yet (SPEC 198). Boot order per SPEC 25 (Invariant 3).
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

#include "airalert/AlertEngine.h"
#include "airalert/ApiHealth.h"
#include "airalert/BackoffPolicy.h"
#include "airalert/NotificationEngine.h"
#include "airalert/SnapshotBuilder.h"
#include "alerts/AlertsClient.h"
#include "hardware/ButtonController.h"
#include "hardware/RelayController.h"
#include "hardware/StatusLed.h"

using namespace airalert;

namespace pins {
constexpr uint8_t RELAY = 14;       // D5 (SPEC 61, 64)
constexpr uint8_t MUTE = 12;        // D6
constexpr uint8_t TEST = 13;        // D7
constexpr uint8_t BUILTIN_LED = 2;  // D4, inverted
} // namespace pins

constexpr bool kRelayActiveHigh = true;   // until commissioning (SPEC 39)
constexpr uint32_t kRelayMaxOnMs = 30000; // SPEC 41

// ---- dev secrets/config on LittleFS (real ConfigManager comes in Phase 5) ----
struct DevConfig {
    String wifiSsid, wifiPass, apiToken;
    bool load() {
        File f = LittleFS.open("/secrets.json", "r");
        if (!f) return false;
        JsonDocument d;
        if (deserializeJson(d, f) != DeserializationError::Ok) return false;
        wifiSsid = d["wifi_ssid"] | "";
        wifiPass = d["wifi_pass"] | "";
        apiToken = d["api_token"] | "";
        return wifiSsid.length() > 0;
    }
    bool save() {
        JsonDocument d;
        d["wifi_ssid"] = wifiSsid;
        d["wifi_pass"] = wifiPass;
        d["api_token"] = apiToken;
        File f = LittleFS.open("/secrets.json", "w");
        if (!f) return false;
        serializeJson(d, f);
        f.close();
        return true;
    }
};

static DevConfig cfg;
static AlertsClient client;
static AlertEngine engine;
static SnapshotBuilder builder;
static ApiHealth health;
static BackoffPolicy backoff;
static NotificationEngine notify;
static RelayController relay;
static ButtonController btnMute, btnTest;
static StatusLed led;
static uint32_t nextPollAt = 0;
static bool ready = false;

static void muteNow(bool longPress) {
    relay.forceOff(); // Invariant 4: relay OFF before anything else
    if (longPress) notify.muteLong(millis());
    else notify.muteShort(millis());
    Serial.printf("[MUTE] %s\n", longPress ? "SNOOZE" : "UNTIL_CLEAR");
}

// Dev selection until the locations UI exists: м. Київ + Київська область.
static const Location kDevSelected[] = {
    {31, LocationType::City, 0, 0},
    {14, LocationType::Oblast, 0, 0},
};

static const char* eventName(AlertEvent e) {
    switch (e) {
        case AlertEvent::Started: return "ALERT_START";
        case AlertEvent::Escalated: return "ALERT_ESCALATED";
        case AlertEvent::CoverageReduced: return "ALERT_COVERAGE_REDUCED";
        case AlertEvent::LocationAdded: return "ALERT_LOCATION_ADDED";
        case AlertEvent::Ended: return "ALERT_END";
        default: return "?";
    }
}

// ---- minimal serial console: provisioning before the Web UI exists ----------
static void handleSerialLine(String line) {
    line.trim();
    if (line.startsWith("setwifi ")) {
        const int sp = line.indexOf(' ', 8);
        if (sp < 0) { Serial.println("[CFG] usage: setwifi <ssid> <pass>"); return; }
        cfg.wifiSsid = line.substring(8, sp);
        cfg.wifiPass = line.substring(sp + 1);
        Serial.printf("[CFG] wifi ssid='%s' %s\n", cfg.wifiSsid.c_str(),
                      cfg.save() ? "saved, restarting" : "SAVE FAILED");
        delay(500);
        ESP.restart();
    } else if (line.startsWith("settoken ")) {
        cfg.apiToken = line.substring(9);
        Serial.printf("[CFG] token %s\n", cfg.save() ? "saved, restarting" : "SAVE FAILED");
        delay(500);
        ESP.restart();
    } else if (line == "show") { // token itself never printed (SPEC 6)
        Serial.printf("[CFG] ssid='%s' token: %s\n", cfg.wifiSsid.c_str(),
                      cfg.apiToken.length() ? "configured" : "MISSING");
    } else if (line == "restart") {
        ESP.restart();
    } else if (line == "mute") {
        muteNow(false);
    } else if (line == "unmute") {
        notify.unmute();
        Serial.println("[MUTE] cleared");
    } else if (line == "test") { // SPEC 54: short relay pulse
        notify.manualTest(millis());
        Serial.println("[TEST] manual test queued");
    } else if (line.startsWith("sim ")) {
#ifdef AIRALERT_DEV
        // dev-only: inject a snapshot to exercise engine->notify->relay
        // with real timing on real hardware (SPEC 199 "safe test load")
        AlertEngine::Snapshot snap{};
        const int sp = line.indexOf(' ', 4);
        const String typeStr = sp > 0 ? line.substring(4, sp) : line.substring(4);
        const String covStr = sp > 0 ? line.substring(sp + 1) : "none";
        const AlertType t = alertTypeFromString(typeStr.c_str());
        Coverage cov = Coverage::None;
        if (covStr == "full") cov = Coverage::Full;
        else if (covStr == "partial") cov = Coverage::Partial;
        snap.types[static_cast<uint8_t>(t)] = {cov, static_cast<uint8_t>(cov != Coverage::None), 1755763200};
        const bool firstSync = !engine.synced();
        EngineEvent ev[8];
        const size_t n = engine.applySnapshot(snap, ev, 8);
        for (size_t i = 0; i < n; ++i) {
            Serial.printf("[ALARM] %s type=%s (SIM)\n", eventName(ev[i].kind),
                          alertTypeToString(ev[i].type));
            notify.onEngineEvent(ev[i], firstSync, millis());
        }
        Serial.printf("[SIM] applied %s=%s events=%u\n", typeStr.c_str(), covStr.c_str(), n);
        nextPollAt = millis() + 300000; // hold real polls off while simulating
#else
        Serial.println("[SIM] dev build only");
#endif
    } else if (line == "status") {
        Serial.printf("[ST] active=%d muted=%d playing=%d relay=%d tripped=%d heap=%u\n",
                      engine.anyActive(), notify.muted(), notify.playing(),
                      relay.isOn(), relay.safetyTripped(), ESP.getFreeHeap());
    } else if (line.length()) {
        Serial.println("[CFG] setwifi <ssid> <pass> | settoken <t> | show | restart | mute | unmute | test | status");
    }
}

static void pollSerial() {
    static String buf;
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (buf.length()) handleSerialLine(buf);
            buf = "";
        } else if (buf.length() < 160) {
            buf += c;
        }
    }
}

void setup() {
    // step 1: relay safe OFF before anything else (Invariant 3)
    relay.begin(pins::RELAY, kRelayActiveHigh, kRelayMaxOnMs);

    Serial.begin(115200);
    Serial.println();
    Serial.printf("[BOOT] AirAlert-ESP8266 %s (%s)\n", AIRALERT_VERSION, __DATE__);
    Serial.printf("[BOOT] reset reason: %s\n", ESP.getResetReason().c_str());

    btnMute.begin(pins::MUTE);
    btnTest.begin(pins::TEST);
    led.begin(pins::BUILTIN_LED, true);

    if (!LittleFS.begin()) Serial.println("[FS] LittleFS mount failed");

    if (!cfg.load()) {
        Serial.println("[CFG] DEVICE NOT READY: no Wi-Fi config (SPEC 162)");
        Serial.println("[CFG] use serial: setwifi <ssid> <pass>, then settoken <token>");
        return;
    }

    builder.setSelected(kDevSelected, 2);
    client.begin(cfg.apiToken);

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    Serial.printf("[WIFI] connecting to '%s'", cfg.wifiSsid.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] FAILED (recovery AP comes in Phase 7)");
        return;
    }
    Serial.printf("[WIFI] IP: %s RSSI: %d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // NTP: required for TLS cert validation (SPEC 83, boot step 8)
    configTime("UTC0", "pool.ntp.org", "time.google.com");
    Serial.print("[NTP] syncing");
    time_t now = 0;
    for (int i = 0; i < 40 && now < 1600000000; ++i) {
        delay(500);
        now = time(nullptr);
        Serial.print('.');
    }
    Serial.println();
    if (now < 1600000000) {
        Serial.println("[NTP] FAILED - TLS will not validate; retrying in loop");
    } else {
        Serial.printf("[NTP] time ok: %lld\n", static_cast<long long>(now));
    }

    if (!client.hasToken()) {
        Serial.println("[API] DEVICE NOT READY: no API token (SPEC 162)");
        return;
    }
    ready = true;
}

static void doPoll() {
    using Kind = AlertsClient::Result::Kind;
    const uint32_t t0 = millis();
    AlertsClient::Result res = client.poll(builder);
    const uint32_t latency = millis() - t0;

    BackoffPolicy::Outcome oc;
    switch (res.kind) {
        case Kind::Ok: {
            health.onContact(millis());
            oc = BackoffPolicy::Outcome::Success;
            const bool firstSync = !engine.synced(); // SPEC 26
            EngineEvent ev[8];
            const size_t n = engine.applySnapshot(builder.snapshot(), ev, 8); // SPEC 168
            for (size_t i = 0; i < n; ++i) {
                Serial.printf("[ALARM] %s type=%s\n", eventName(ev[i].kind),
                              alertTypeToString(ev[i].type));
                notify.onEngineEvent(ev[i], firstSync, millis());
            }
            Serial.printf("[API] 200 ok alerts=%u skipped=%u active=%d latency=%lums heap=%u\n",
                          res.stats.total, res.stats.skipped, engine.anyActive(),
                          (unsigned long)latency, ESP.getFreeHeap());
            break;
        }
        case Kind::NotModified:
            health.onContact(millis());
            oc = BackoffPolicy::Outcome::NotModified;
            Serial.printf("[API] 304 not modified latency=%lums heap=%u\n",
                          (unsigned long)latency, ESP.getFreeHeap());
            break;
        case Kind::AuthError:
            health.onFailure();
            oc = BackoffPolicy::Outcome::AuthError;
            Serial.println("[API] API_AUTH_ERROR 401 - check token (SPEC 163)");
            break;
        case Kind::Forbidden:
            health.onFailure();
            oc = BackoffPolicy::Outcome::AuthError;
            Serial.println("[API] API_ACCESS_FORBIDDEN 403 (SPEC 164)");
            break;
        case Kind::RateLimited:
            health.onFailure();
            oc = BackoffPolicy::Outcome::RateLimited;
            Serial.printf("[API] API_RATE_LIMIT 429 retry-after=%us\n", res.retryAfterSec);
            break;
        case Kind::ParseError:
            health.onFailure(); // snapshot NOT applied - Invariant 6
            oc = BackoffPolicy::Outcome::ParseError;
            Serial.println("[API] API_PARSE_ERROR - keeping previous state");
            break;
        default:
            health.onFailure();
            oc = BackoffPolicy::Outcome::NetError;
            Serial.printf("[API] API_OFFLINE code=%d heap=%u\n", res.httpCode, ESP.getFreeHeap());
            break;
    }

    const uint32_t delayMs =
        backoff.next(oc, res.retryAfterSec, static_cast<uint8_t>(ESP.random() & 0xFF));
    nextPollAt = millis() + delayMs;
}

static void updateLed(bool sirenOn) {
    using Mode = StatusLed::Mode;
    if (sirenOn) { led.setMode(Mode::SirenOn); return; }
    if (!engine.anyActive()) { led.setMode(Mode::Off); return; }
    if (notify.muted()) { led.setMode(Mode::Muted); return; } // SPEC 51
    bool full = false;
    for (uint8_t i = 0; i < kAlertTypeCount; ++i)
        if (engine.status(static_cast<AlertType>(i)).coverage == Coverage::Full)
            full = true;
    led.setMode(full ? Mode::AlertFull : Mode::AlertPartial);
}

// SPEC 116: non-blocking tick pipeline
void loop() {
    const uint32_t now = millis();
    pollSerial();

    // buttons work even before Wi-Fi/API are ready
    switch (btnMute.tick(now)) {
        case ButtonController::Event::LongPress: muteNow(true); break;
        case ButtonController::Event::Release:
            if (btnMute.wasShortPress()) muteNow(false);
            break;
        default: break;
    }
    if (btnTest.tick(now) == ButtonController::Event::LongPress) { // SPEC 54: hold 2 s
        notify.manualTest(now);
        Serial.println("[TEST] button");
    }

    const bool siren = notify.tick(now);
    relay.tick(siren, now);
    updateLed(relay.isOn());
    led.tick(now);

    if (!ready) return;

    if (static_cast<int32_t>(now - nextPollAt) >= 0) doPoll();

    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 30000) {
        lastBeat = now;
        Serial.printf("[SYS] uptime=%lus heap=%u active=%d stale=%d online=%d relay=%d muted=%d\n",
                      (unsigned long)(now / 1000), ESP.getFreeHeap(),
                      engine.anyActive(), health.stale(now), health.online(),
                      relay.isOn(), notify.muted());
    }
}
