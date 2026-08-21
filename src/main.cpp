// AirAlert-ESP8266 — main wiring. Boot order per SPEC 25 (Invariant 3),
// non-blocking tick pipeline per SPEC 116.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

#include "airalert/AlertEngine.h"
#include "airalert/ApiHealth.h"
#include "airalert/BackoffPolicy.h"
#include "airalert/Config.h"
#include "airalert/NotificationEngine.h"
#include "airalert/SnapshotBuilder.h"
#include "airalert/StartupPolicy.h"
#include "alerts/AlertsClient.h"
#include "alerts/LocationCatalog.h"
#include "config/ConfigStore.h"
#include "config/SecretsStore.h"
#include "hardware/ButtonController.h"
#include "hardware/RelayController.h"
#include "hardware/Indication.h"
#include "network/WifiService.h"
#include "storage/EventLogStore.h"
#include "storage/StateStore.h"
#include "web/WebUi.h"

using namespace airalert;

namespace pins {
constexpr uint8_t RELAY = 14;       // D5 (SPEC 61, 64)
constexpr uint8_t MUTE = 12;        // D6
constexpr uint8_t TEST = 13;        // D7
constexpr uint8_t BUILTIN_LED = 2;  // D4, inverted — SYSTEM indicator
constexpr uint8_t ALERT_OUT = 16;   // D0 — alert LED or indication relay
} // namespace pins

static SecretsStore secrets;
static AppConfig appCfg;
static ConfigStore configStore;
static StateStore stateStore;
static PersistedState pstate;
static EventLogStore eventLog;
static AlertsClient client;
static AlertEngine engine;
static SnapshotBuilder builder;
static ApiHealth health;
static BackoffPolicy backoff;
static NotificationEngine notify;
static RelayController relay;
static ButtonController btnMute, btnTest;
static Indication led;
static WifiService wifi;
static WebUi web;
static LocationCatalog catalog;

static uint32_t nextPollAt = 0;
static bool ntpStarted = false;
static bool ntpSynced = false;
static bool wasOnline = false;
static bool otaInProgress = false;

static bool apiReady() {
    return wifi.online() && ntpSynced && client.hasToken();
}

static void muteNow(bool longPress) {
    relay.forceOff(); // Invariant 4: relay OFF before anything else
    if (longPress) notify.muteLong(millis());
    else notify.muteShort(millis());
    eventLog.log(LogEvent::Mute, longPress ? "scope=snooze" : "scope=until_clear");
}

static void prepareOta() { // Invariant 8
    otaInProgress = true;
    notify.stopAll();
    relay.forceOff();
}

// Config -> subsystems (SPEC 25 step 4; re-applied after Web UI edits)
static void applyConfig() {
    engine.setConfig({appCfg.startConfirmations, appCfg.endConfirmations,
                      appCfg.partialActive});
    notify.setMuteConfig({appCfg.snoozeMinutes * 60000u, appCfg.muteAllAlertTypes});
    for (uint8_t i = 0; i < kAlertTypeCount; ++i)
        notify.setProfile(static_cast<AlertType>(i), appCfg.profiles[i]);
    relay.begin(pins::RELAY, appCfg.relayActiveHigh, appCfg.relayMaxOnMs);
    backoff.setConfig({appCfg.pollIntervalSec * 1000u, 120000, 60000, 300000, 10});
    health.setConfig({appCfg.apiStaleAfterSec * 1000u});
    builder.setSelected(appCfg.selected, appCfg.selectedCount);
    builder.setCatalog(&catalog);
    led.begin(pins::BUILTIN_LED, true, pins::ALERT_OUT,
              appCfg.alertIndicatorActiveHigh, appCfg.alertIndicatorSteady);
}

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

// Shared by the real poller and the dev `sim` command.
static void applyAndNotify(const AlertEngine::Snapshot& snap, bool simulated) {
    const bool firstSync = !engine.synced(); // SPEC 26

    auto startupMode = NotificationEngine::StartupMode::Normal;
    if (firstSync) { // SPEC 26-28
        const uint32_t fp = StartupPolicy::fingerprint(snap);
        const int64_t nowUtc = time(nullptr) > 1600000000 ? time(nullptr) : 0;
        startupMode = StartupPolicy::shouldNotify(
                          fp, pstate.alertFingerprint, pstate.startupNotifAtUtc,
                          nowUtc, appCfg.startupCooldownSec)
                          ? NotificationEngine::StartupMode::Short
                          : NotificationEngine::StartupMode::Silent;
    }

    EngineEvent ev[8];
    const size_t n = engine.applySnapshot(snap, ev, 8); // SPEC 168
    for (size_t i = 0; i < n; ++i) {
        Serial.printf("[ALARM] %s type=%s%s\n", eventName(ev[i].kind),
                      alertTypeToString(ev[i].type), simulated ? " (SIM)" : "");
        auto mode = startupMode;
        // SPEC 20: partial-only alert with siren disabled for partial
        if (ev[i].kind == AlertEvent::Started && !appCfg.partialSiren &&
            engine.status(ev[i].type).coverage == Coverage::Partial)
            mode = NotificationEngine::StartupMode::Silent;
        notify.onEngineEvent(ev[i], mode, millis());
        switch (ev[i].kind) {
            case AlertEvent::Started:
                eventLog.log(engine.status(ev[i].type).coverage == Coverage::Full
                                 ? LogEvent::AlertFull : LogEvent::AlertPartial,
                             alertTypeToString(ev[i].type));
                break;
            case AlertEvent::Ended:
                eventLog.log(LogEvent::AlertEnd, alertTypeToString(ev[i].type));
                break;
            default: break;
        }
    }
    if (n > 0 || firstSync) { // SPEC 96: persist only on change
        pstate.alertFingerprint = StartupPolicy::fingerprint(snap);
        if (n > 0 || startupMode == NotificationEngine::StartupMode::Short)
            pstate.startupNotifAtUtc = time(nullptr);
        stateStore.save(pstate);
    }
}

static void doPoll() {
    using Kind = AlertsClient::Result::Kind;
    const uint32_t t0 = millis();
    AlertsClient::Result res = client.poll(builder);
    const uint32_t latency = millis() - t0;

    BackoffPolicy::Outcome oc;
    switch (res.kind) {
        case Kind::Ok:
            if (!health.online()) eventLog.log(LogEvent::ApiOnline);
            health.onContact(millis());
            oc = BackoffPolicy::Outcome::Success;
            applyAndNotify(builder.snapshot(), false);
            Serial.printf("[API] 200 ok alerts=%u skipped=%u active=%d latency=%lums heap=%u\n",
                          res.stats.total, res.stats.skipped, engine.anyActive(),
                          (unsigned long)latency, ESP.getFreeHeap());
            break;
        case Kind::NotModified:
            if (!health.online()) eventLog.log(LogEvent::ApiOnline);
            health.onContact(millis());
            oc = BackoffPolicy::Outcome::NotModified;
            Serial.printf("[API] 304 not modified latency=%lums heap=%u\n",
                          (unsigned long)latency, ESP.getFreeHeap());
            break;
        case Kind::AuthError:
            health.onFailure();
            oc = BackoffPolicy::Outcome::AuthError;
            eventLog.log(LogEvent::Api401);
            break;
        case Kind::Forbidden:
            health.onFailure();
            oc = BackoffPolicy::Outcome::AuthError;
            eventLog.log(LogEvent::Api403);
            break;
        case Kind::RateLimited:
            health.onFailure();
            oc = BackoffPolicy::Outcome::RateLimited;
            eventLog.log(LogEvent::Api429);
            break;
        case Kind::ParseError:
            health.onFailure(); // snapshot NOT applied - Invariant 6
            oc = BackoffPolicy::Outcome::ParseError;
            eventLog.log(LogEvent::ApiParseError);
            break;
        default:
            if (health.online()) eventLog.log(LogEvent::ApiOffline);
            health.onFailure();
            oc = BackoffPolicy::Outcome::NetError;
            Serial.printf("[API] API_OFFLINE code=%d heap=%u\n", res.httpCode, ESP.getFreeHeap());
            break;
    }
    nextPollAt = millis() +
                 backoff.next(oc, res.retryAfterSec, static_cast<uint8_t>(ESP.random() & 0xFF));
}

// ---- serial console ---------------------------------------------------------
static void handleSerialLine(String line) {
    line.trim();
    if (line.startsWith("setwifi ")) {
        const int sp = line.indexOf(' ', 8);
        if (sp < 0) { Serial.println("[CFG] usage: setwifi <ssid> <pass>"); return; }
        secrets.wifiSsid = line.substring(8, sp);
        secrets.wifiPass = line.substring(sp + 1);
        Serial.printf("[CFG] wifi ssid='%s' %s\n", secrets.wifiSsid.c_str(),
                      secrets.save() ? "saved, restarting" : "SAVE FAILED");
        delay(500);
        ESP.restart();
    } else if (line.startsWith("settoken ")) {
        secrets.apiToken = line.substring(9);
        Serial.printf("[CFG] token %s\n", secrets.save() ? "saved, restarting" : "SAVE FAILED");
        delay(500);
        ESP.restart();
    } else if (line.startsWith("setpass ")) {
        secrets.setWebPassword(line.substring(8));
        Serial.printf("[CFG] web password %s\n", secrets.save() ? "saved" : "SAVE FAILED");
    } else if (line == "forgetwifi") { // SPEC 82
        eventLog.log(LogEvent::ConfigChanged, "wifi_forget");
        wifi.forget();
    } else if (line == "show") { // secrets never printed (SPEC 6)
        Serial.printf("[CFG] ssid='%s' token: %s webpass: %s\n", secrets.wifiSsid.c_str(),
                      secrets.apiToken.length() ? "configured" : "MISSING",
                      secrets.hasWebPassword() ? "set" : "NOT SET");
    } else if (line == "restart") {
        ESP.restart();
    } else if (line == "mute") {
        muteNow(false);
    } else if (line == "unmute") {
        notify.unmute();
        eventLog.log(LogEvent::Unmute);
    } else if (line == "test") { // SPEC 54
        notify.manualTest(millis());
        eventLog.log(LogEvent::Test, "source=serial");
    } else if (line == "status") {
        Serial.printf("[ST] wifi=%d ntp=%d active=%d muted=%d playing=%d relay=%d tripped=%d heap=%u\n",
                      wifi.online(), ntpSynced, engine.anyActive(), notify.muted(),
                      notify.playing(), relay.isOn(), relay.safetyTripped(), ESP.getFreeHeap());
    } else if (line == "config") {
        JsonDocument d;
        configToJson(appCfg, d); // never contains secrets (SPEC 91)
        serializeJson(d, Serial);
        Serial.println();
    } else if (line == "log") {
        for (const char* path : {"/log/ev.0", "/log/ev.1"}) {
            File f = LittleFS.open(path, "r");
            if (!f) continue;
            while (f.available()) Serial.write(f.read());
            f.close();
        }
    } else if (line.startsWith("sim ")) {
#ifdef AIRALERT_DEV
        AlertEngine::Snapshot snap{};
        const int sp = line.indexOf(' ', 4);
        const String typeStr = sp > 0 ? line.substring(4, sp) : line.substring(4);
        const String covStr = sp > 0 ? line.substring(sp + 1) : "none";
        Coverage cov = covStr == "full" ? Coverage::Full
                       : covStr == "partial" ? Coverage::Partial : Coverage::None;
        snap.types[static_cast<uint8_t>(alertTypeFromString(typeStr.c_str()))] =
            {cov, static_cast<uint8_t>(cov != Coverage::None), 1755763200};
        applyAndNotify(snap, true);
        nextPollAt = millis() + 300000; // hold real polls off while simulating
        Serial.printf("[SIM] applied %s=%s\n", typeStr.c_str(), covStr.c_str());
#else
        Serial.println("[SIM] dev build only");
#endif
    } else if (line.length()) {
        Serial.println("[CFG] setwifi|settoken|setpass|forgetwifi|show|restart|mute|unmute|test|status|config|log|sim");
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

// ---- boot -------------------------------------------------------------------
void setup() {
    // step 1: relay safe OFF before anything else (Invariant 3).
    // Default polarity assumption until config loads (SPEC 39).
    relay.begin(pins::RELAY, true, 30000);

    Serial.begin(115200);
    Serial.println();
    Serial.printf("[BOOT] AirAlert-ESP8266 %s (%s)\n", AIRALERT_VERSION, __DATE__);
    Serial.printf("[BOOT] reset reason: %s\n", ESP.getResetReason().c_str());

    btnMute.begin(pins::MUTE);
    btnTest.begin(pins::TEST);
    // config not loaded yet: safe defaults, re-begun in applyConfig()
    led.begin(pins::BUILTIN_LED, true, pins::ALERT_OUT, true, false);

    if (!LittleFS.begin()) Serial.println("[FS] LittleFS mount failed");

    if (!configStore.load(appCfg)) {
        appCfg = AppConfig{}; // corrupt/missing -> defaults (SPEC 128)
        Serial.println("[CFG] using default config");
    }
    applyConfig();
    stateStore.load(pstate);
    eventLog.begin();
    eventLog.log(LogEvent::Boot, ESP.getResetReason().c_str());

    secrets.load();
    client.begin(secrets.apiToken);
    wifi.begin(&secrets); // STA or provisioning AP (SPEC 78-81)

    web.begin({&appCfg, &engine, &notify, &health, &relay, &configStore, &secrets,
               &eventLog, &wifi,
               [] { applyConfig(); },
               [](const String& t) {
                   secrets.apiToken = t;
                   secrets.save();
                   client.begin(t);
                   nextPollAt = millis(); // poll with the new token immediately
               },
               [](bool longPress) { muteNow(longPress); },
               [] {
                   notify.unmute();
                   eventLog.log(LogEvent::Unmute);
               },
               [] { prepareOta(); }});

    if (!secrets.apiToken.length())
        Serial.println("[API] DEVICE NOT READY: no API token (SPEC 162)");
}

static void updateLed(bool sirenOn) {
    // SYSTEM channel: solid = fully operational
    Indication::SysState sys;
    if (wifi.state() == WifiService::State::Provisioning ||
        !secrets.apiToken.length() || otaInProgress)
        sys = Indication::SysState::Setup;
    else if (wifi.online() && ntpSynced && health.online() && !health.stale(millis()))
        sys = Indication::SysState::Ok;
    else
        sys = Indication::SysState::Degraded;
    led.setSystem(sys);

    // ALERT channel
    auto view = Indication::AlertView::None;
    if (engine.anyActive()) {
        bool full = false;
        for (uint8_t i = 0; i < kAlertTypeCount; ++i)
            if (engine.status(static_cast<AlertType>(i)).coverage == Coverage::Full)
                full = true;
        if (full) view = Indication::AlertView::Full;
        else if (appCfg.partialLed) view = Indication::AlertView::Partial; // SPEC 20
    }
    led.setAlert(view, notify.muted(), sirenOn);
}

// SPEC 116: non-blocking tick pipeline
void loop() {
    const uint32_t now = millis();
    pollSerial();
    wifi.tick(now);
    web.tick();
    if (otaInProgress) return; // Invariant 8: nothing else runs during OTA

    // Wi-Fi online/offline transitions -> log + NTP kick-off
    if (wifi.online() != wasOnline) {
        wasOnline = wifi.online();
        eventLog.log(wasOnline ? LogEvent::WifiConnected : LogEvent::WifiDisconnected);
        if (wasOnline && !ntpStarted) {
            configTime("UTC0", "pool.ntp.org", "time.google.com"); // SPEC 83
            ntpStarted = true;
        }
    }
    if (ntpStarted && !ntpSynced && time(nullptr) > 1600000000) {
        ntpSynced = true;
        Serial.printf("[NTP] time ok: %lld\n", static_cast<long long>(time(nullptr)));
    }

    switch (btnMute.tick(now)) {
        case ButtonController::Event::LongPress: muteNow(true); break;
        case ButtonController::Event::Release:
            if (btnMute.wasShortPress()) muteNow(false);
            break;
        default: break;
    }
    if (btnTest.tick(now) == ButtonController::Event::LongPress) { // SPEC 54
        notify.manualTest(now);
        eventLog.log(LogEvent::Test, "source=button");
    }

    const bool siren = notify.tick(now);
    const bool trippedBefore = relay.safetyTripped();
    relay.tick(siren, now);
    if (relay.safetyTripped() && !trippedBefore)
        eventLog.log(LogEvent::RelaySafetyTrip); // Invariant 2 fired

    updateLed(relay.isOn());
    led.tick(now);

    if (apiReady() && static_cast<int32_t>(now - nextPollAt) >= 0) doPoll();

    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 30000) {
        lastBeat = now;
        Serial.printf("[SYS] uptime=%lus heap=%u wifi=%d active=%d stale=%d online=%d relay=%d muted=%d\n",
                      (unsigned long)(now / 1000), ESP.getFreeHeap(), wifi.online(),
                      engine.anyActive(), health.stale(now), health.online(),
                      relay.isOn(), notify.muted());
    }
}
