#pragma once
// Embedded Web UI + versioned REST API (SPEC 67-77, 97-101, 122-127).
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <functional>
#include "airalert/AlertEngine.h"
#include "airalert/ApiHealth.h"
#include "airalert/Config.h"
#include "airalert/NotificationEngine.h"
#include "config/ConfigStore.h"
#include "config/SecretsStore.h"
#include "hardware/RelayController.h"
#include "storage/EventLogStore.h"

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
        std::function<void()> applyConfig;      // config -> subsystems
        std::function<void(const String&)> setApiToken;
        std::function<void(bool)> mute;         // arg: long/snooze
        std::function<void()> unmute;
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

    Deps d_;
    ESP8266WebServer server_{80};
    Session sessions_[kSessions];
    uint8_t loginFails_ = 0;
    uint32_t lockUntil_ = 0;
    uint32_t rebootAt_ = 0;
};
