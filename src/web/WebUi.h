#pragma once
// Embedded Web UI + versioned REST API (SPEC 67-77, 97-101, 122-127).
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <functional>
#include "airalert/AlertEngine.h"
#include "airalert/ApiHealth.h"
#include "airalert/Config.h"
#include "airalert/NotificationEngine.h"
#include "airalert/SnapshotBuilder.h"
#include "config/ConfigStore.h"
#include "config/SecretsStore.h"
#include "hardware/RelayController.h"
#include "network/WifiService.h"
#include "storage/EventLogStore.h"

// Per-location threat detail snapshot, copied from SnapshotBuilder after each
// successful poll so the dashboard never reads a mid-parse builder state.
struct ActiveLocationView {
    struct Threat {
        uint32_t startedAt; // unix seconds
        uint8_t type;       // AlertType
        uint8_t coverage;   // Coverage (never None here)
    };
    uint8_t locCount = 0;
    uint16_t uid[airalert::SnapshotBuilder::kMaxSelected] = {};
    uint8_t threatCount[airalert::SnapshotBuilder::kMaxSelected] = {};
    Threat threats[airalert::SnapshotBuilder::kMaxSelected][airalert::kAlertTypeCount];
};

class WebUi {
public:
    struct Deps {
        airalert::AppConfig* cfg;
        airalert::AlertEngine* engine;
        airalert::NotificationEngine* notify;
        airalert::ApiHealth* health;
        RelayController* relay;
        ConfigStore* configStore;
        SecretsStore* secrets;
        EventLogStore* log;
        WifiService* wifi;
        const struct ActiveLocationView* activeView; // dashboard detail (SPEC 71)
        std::function<void()> applyConfig;      // config -> subsystems
        std::function<void(const String&)> setApiToken;
        std::function<void()> refreshAlerts;    // location selection changed
        std::function<void(bool)> mute;         // arg: long/snooze
        std::function<void()> unmute;
        std::function<void()> prepareOta;       // Invariant 8: relay off, queue clear
        std::function<void()> finishFailedOta;  // resume normal loop after failure
        const bool* ntpSynced;
        const int* lastApiHttpCode;
        const String* lastApiError;
    };

    void begin(const Deps& d);
    void tick();

private:
    // sessions (SPEC 100): token in X-Auth header on every request -> no
    // cookies, hence no CSRF surface
    struct Session { char token[33] = {0}; uint32_t expiresAt = 0; };
    static constexpr uint8_t kSessions = 4;
    static constexpr uint32_t kSessionTtlMs = 24u * 3600u * 1000u;

    bool authed();
    void sendError(int code, const char* errCode, const char* msg);
    void sendJson(const JsonDocument& d, int code = 200);
    void handleLogin();
    void handleStatus();
    void handleConfigGet();
    void handleConfigPut();
    void handleLocationsPut();
    void handleTokenPut();
    void handleEvents();
    void handleSystem();
    void handleScan();
    void handleSetup();
    void handleOtaUpload();
    void handleOtaUrl();

    Deps d_;
    ESP8266WebServer server_{80};
    Session sessions_[kSessions];
    uint8_t loginFails_ = 0;
    uint32_t lockUntil_ = 0;
    uint32_t rebootAt_ = 0;
    bool otaUploadAuthorized_ = false;
    bool otaUploadStarted_ = false;
    bool otaUploadSucceeded_ = false;
};
