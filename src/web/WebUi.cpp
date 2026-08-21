#include "WebUi.h"
#include <LittleFS.h>
#include "WebAssets.h"

using namespace airalert;

static const char* coverageName(Coverage c) {
    return c == Coverage::Full ? "full" : c == Coverage::Partial ? "partial" : "none";
}

void WebUi::begin(const Deps& d) {
    d_ = d;
    server_.collectHeaders("X-Auth"); // core 3.x variadic form

    server_.on("/", HTTP_GET, [this] {
        server_.sendHeader("Content-Encoding", "gzip");
        server_.send_P(200, ASSET_INDEX_HTML_MIME,
                       reinterpret_cast<const char*>(ASSET_INDEX_HTML), ASSET_INDEX_HTML_LEN);
    });
    server_.on("/locations.json", HTTP_GET, [this] {
        server_.sendHeader("Content-Encoding", "gzip");
        server_.sendHeader("Cache-Control", "max-age=86400");
        server_.send_P(200, ASSET_LOCATIONS_JSON_MIME,
                       reinterpret_cast<const char*>(ASSET_LOCATIONS_JSON), ASSET_LOCATIONS_JSON_LEN);
    });

    server_.on("/api/v1/login", HTTP_POST, [this] { handleLogin(); });
    server_.on("/api/v1/status", HTTP_GET, [this] { handleStatus(); });
    server_.on("/api/v1/config", HTTP_GET, [this] { handleConfigGet(); });
    server_.on("/api/v1/config", HTTP_PUT, [this] { handleConfigPut(); });
    server_.on("/api/v1/locations", HTTP_PUT, [this] { handleLocationsPut(); });
    server_.on("/api/v1/secrets/api-token", HTTP_PUT, [this] { handleTokenPut(); });
    server_.on("/api/v1/events", HTTP_GET, [this] { handleEvents(); });
    server_.on("/api/v1/system", HTTP_GET, [this] { handleSystem(); });

    server_.on("/api/v1/mute", HTTP_POST, [this] {
        if (!authed()) return;
        d_.mute(false);
        server_.send(200, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/v1/unmute", HTTP_POST, [this] {
        if (!authed()) return;
        d_.unmute();
        server_.send(200, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/v1/test/relay", HTTP_POST, [this] { // SPEC 54, 77
        if (!authed()) return;
        d_.notify->manualTest(millis());
        d_.log->log(LogEvent::Test, "source=web");
        server_.send(200, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/v1/relay/off", HTTP_POST, [this] { // SPEC 125 manual override
        if (!authed()) return;
        d_.notify->stopAll();
        d_.relay->forceOff();
        server_.send(200, "application/json", "{\"ok\":true}");
    });
    server_.on("/api/v1/reboot", HTTP_POST, [this] {
        if (!authed()) return;
        server_.send(200, "application/json", "{\"ok\":true}");
        rebootAt_ = millis() + 300;
    });

    server_.onNotFound([this] { sendError(404, "NOT_FOUND", "unknown endpoint"); });
    server_.begin();
}

void WebUi::tick() {
    server_.handleClient();
    if (rebootAt_ && static_cast<int32_t>(millis() - rebootAt_) >= 0) ESP.restart();
}

// ---- auth -------------------------------------------------------------------

bool WebUi::authed() {
    const String tok = server_.header("X-Auth");
    if (tok.length() == 32) {
        for (auto& s : sessions_) {
            if (s.token[0] && tok == s.token &&
                static_cast<int32_t>(millis() - s.expiresAt) < 0)
                return true;
        }
    }
    sendError(401, "UNAUTHORIZED", "login required");
    return false;
}

void WebUi::handleLogin() {
    if (static_cast<int32_t>(millis() - lockUntil_) < 0) { // SPEC 100 rate limit
        sendError(429, "LOCKED", "too many attempts");
        return;
    }
    JsonDocument d;
    if (deserializeJson(d, server_.arg("plain")) != DeserializationError::Ok ||
        !d_.secrets->hasWebPassword() ||
        !d_.secrets->checkWebPassword(d["password"] | "")) {
        if (++loginFails_ >= 5) {
            loginFails_ = 0;
            lockUntil_ = millis() + 60000;
        }
        sendError(401, "BAD_CREDENTIALS",
                  d_.secrets->hasWebPassword() ? "invalid password"
                                               : "password not set (serial: setpass)");
        return;
    }
    loginFails_ = 0;
    // oldest slot gets recycled
    Session* slot = &sessions_[0];
    for (auto& s : sessions_)
        if (static_cast<int32_t>(s.expiresAt - slot->expiresAt) < 0) slot = &s;
    snprintf(slot->token, sizeof slot->token, "%08x%08x%08x%08x",
             static_cast<unsigned>(ESP.random()), static_cast<unsigned>(ESP.random()),
             static_cast<unsigned>(ESP.random()), static_cast<unsigned>(ESP.random()));
    slot->expiresAt = millis() + kSessionTtlMs;
    JsonDocument out;
    out["token"] = slot->token;
    sendJson(out);
}

// ---- handlers ---------------------------------------------------------------

void WebUi::handleStatus() { // SPEC 127; auth required (SPEC 99 default)
    if (!authed()) return;
    JsonDocument d;
    d["device"] = d_.cfg->deviceName;
    d["firmware"] = AIRALERT_VERSION;
    d["uptime_sec"] = millis() / 1000;
    d["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    d["wifi"]["rssi"] = WiFi.RSSI();
    const uint32_t since = d_.health->sinceContactMs(millis());
    d["api"]["online"] = d_.health->online();
    d["api"]["stale"] = d_.health->stale(millis());
    d["api"]["ever_synced"] = d_.engine->synced();
    if (since == UINT32_MAX) d["api"]["last_contact_sec"] = nullptr;
    else d["api"]["last_contact_sec"] = since / 1000;
    d["api"]["token_present"] = d_.secrets->apiToken.length() > 0; // value never sent (SPEC 6)
    JsonArray types = d["alerts"]["types"].to<JsonArray>();
    bool any = false;
    for (uint8_t i = 0; i < kAlertTypeCount; ++i) {
        const auto& st = d_.engine->status(static_cast<AlertType>(i));
        if (st.state != AlertState::Active) continue;
        any = true;
        JsonObject o = types.add<JsonObject>();
        o["type"] = alertTypeToString(static_cast<AlertType>(i));
        o["coverage"] = coverageName(st.coverage);
        o["started_at"] = st.startedAt;
    }
    d["alerts"]["active"] = any;
    d["alerts"]["muted"] = d_.notify->muted();
    d["alerts"]["count"] = types.size();
    d["relay"]["active"] = d_.relay->isOn();
    d["relay"]["tripped"] = d_.relay->safetyTripped();
    sendJson(d);
}

void WebUi::handleConfigGet() {
    if (!authed()) return;
    JsonDocument d;
    configToJson(*d_.cfg, d); // never contains secrets (SPEC 91)
    sendJson(d);
}

void WebUi::handleConfigPut() {
    if (!authed()) return;
    JsonDocument d;
    if (deserializeJson(d, server_.arg("plain")) != DeserializationError::Ok) {
        sendError(400, "BAD_JSON", "invalid JSON body");
        return;
    }
    AppConfig next = *d_.cfg; // start from current, overlay body (SPEC 90 style)
    configFromJson(d.as<JsonVariantConst>(), next);
    const ConfigError err = validateConfig(next); // backend validation (SPEC 155)
    if (err != ConfigError::None) {
        sendError(422, "INVALID_CONFIG", "value out of allowed range");
        return;
    }
    *d_.cfg = next;
    d_.configStore->save(*d_.cfg);
    d_.applyConfig();
    d_.log->log(LogEvent::ConfigChanged, "source=web");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUi::handleLocationsPut() {
    if (!authed()) return;
    JsonDocument d;
    if (deserializeJson(d, server_.arg("plain")) != DeserializationError::Ok ||
        d["locations"].isNull()) {
        sendError(400, "BAD_JSON", "expected {locations:[...]}");
        return;
    }
    AppConfig next = *d_.cfg;
    JsonDocument wrap; // reuse the config parser for just the locations array
    wrap["locations"] = d["locations"];
    configFromJson(wrap.as<JsonVariantConst>(), next);
    if (validateConfig(next) != ConfigError::None || next.selectedCount == 0) {
        sendError(422, "INVALID_CONFIG", "bad location list");
        return;
    }
    *d_.cfg = next;
    d_.configStore->save(*d_.cfg);
    d_.applyConfig();
    d_.log->log(LogEvent::ConfigChanged, "locations");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUi::handleTokenPut() { // write-only (SPEC 91)
    if (!authed()) return;
    JsonDocument d;
    const String tok = (deserializeJson(d, server_.arg("plain")) == DeserializationError::Ok)
                           ? (d["token"] | "") : String();
    if (tok.length() < 10) {
        sendError(422, "BAD_TOKEN", "token too short");
        return;
    }
    d_.setApiToken(tok);
    d_.log->log(LogEvent::ConfigChanged, "api_token");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void WebUi::handleEvents() {
    if (!authed()) return;
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, "application/x-ndjson", "");
    for (const char* path : {"/log/ev.0", "/log/ev.1"}) {
        File f = LittleFS.open(path, "r");
        if (!f) continue;
        uint8_t buf[256];
        size_t n;
        while ((n = f.read(buf, sizeof buf)) > 0)
            server_.sendContent(reinterpret_cast<const char*>(buf), n);
        f.close();
    }
    server_.sendContent("");
}

void WebUi::handleSystem() { // SPEC 114, 126
    if (!authed()) return;
    JsonDocument d;
    d["firmware"] = AIRALERT_VERSION;
    d["build"] = __DATE__ " " __TIME__;
    d["core"] = ESP.getCoreVersion();
    d["reset_reason"] = ESP.getResetReason();
    d["uptime_sec"] = millis() / 1000;
    d["heap_free"] = ESP.getFreeHeap();
    d["heap_max_block"] = ESP.getMaxFreeBlockSize();
    d["heap_frag_pct"] = ESP.getHeapFragmentation();
    d["flash_size"] = ESP.getFlashChipRealSize();
    d["sketch_size"] = ESP.getSketchSize();
    d["sketch_free"] = ESP.getFreeSketchSpace();
    FSInfo fs;
    if (LittleFS.info(fs)) {
        d["fs_used"] = fs.usedBytes;
        d["fs_total"] = fs.totalBytes;
    }
    d["ip"] = WiFi.localIP().toString();
    d["rssi"] = WiFi.RSSI();
    d["catalogue"] = "v1 (155 locations)";
    sendJson(d);
}

// ---- helpers ----------------------------------------------------------------

void WebUi::sendError(int code, const char* errCode, const char* msg) { // SPEC 156
    JsonDocument d;
    d["ok"] = false;
    d["error"]["code"] = errCode;
    d["error"]["message"] = msg;
    sendJson(d, code);
}

void WebUi::sendJson(const JsonDocument& d, int code) {
    String out;
    serializeJson(d, out);
    server_.send(code, "application/json", out);
}
