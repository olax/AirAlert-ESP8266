#pragma once
// Wi-Fi lifecycle (SPEC 24, 78-82): STA connect with timeout, recovery
// AP+STA with captive-portal DNS, credential reset. Non-blocking.
#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include "config/SecretsStore.h"

class WifiService {
public:
    enum class State : uint8_t { Connecting, Connected, Provisioning };

    static constexpr uint32_t kStaTimeoutMs = 120000; // SPEC 81
    static constexpr uint32_t kRetryIntervalMs = 30000;

    void begin(SecretsStore* secrets) {
        secrets_ = secrets;
        apSsid_ = "AirAlert-" + String(ESP.getChipId() & 0xFFFF, HEX);
        apSsid_.toUpperCase();
        // per-device AP password (SPEC 79: no universal default). Printed to
        // serial on provisioning entry; stable across reboots (chip id based).
        char pass[16];
        snprintf(pass, sizeof pass, "aa-%08x", ESP.getChipId() * 2654435761u);
        apPass_ = pass;
        if (secrets_->wifiSsid.length()) startSta();
        else startProvisioning("no Wi-Fi credentials");
    }

    void tick(uint32_t now) {
        if (state_ == State::Provisioning) {
            dns_.processNextRequest();
            // keep retrying STA in the background if we have credentials (SPEC 81)
            if (secrets_->wifiSsid.length() && WiFi.status() == WL_CONNECTED)
                becomeConnected();
            else if (secrets_->wifiSsid.length() &&
                     static_cast<int32_t>(now - nextRetryAt_) >= 0) {
                nextRetryAt_ = now + kRetryIntervalMs;
                WiFi.begin(secrets_->wifiSsid, secrets_->wifiPass);
            }
            return;
        }
        if (state_ == State::Connecting) {
            if (WiFi.status() == WL_CONNECTED) becomeConnected();
            else if (static_cast<int32_t>(now - staDeadline_) >= 0)
                startProvisioning("STA timeout");
            return;
        }
        // Connected: watch for drops; lwIP auto-reconnects, we only track state
        if (WiFi.status() != WL_CONNECTED && state_ == State::Connected) {
            state_ = State::Connecting;
            staDeadline_ = now + kStaTimeoutMs;
        }
    }

    // From portal/serial: try new credentials immediately.
    void applyNewCredentials() { startSta(); }

    void forget() { // SPEC 82
        secrets_->wifiSsid = "";
        secrets_->wifiPass = "";
        secrets_->save();
        startProvisioning("credentials cleared");
    }

    State state() const { return state_; }
    bool online() const { return state_ == State::Connected; }
    const String& apSsid() const { return apSsid_; }
    const String& apPass() const { return apPass_; }

private:
    void startSta() {
        dns_.stop();
        WiFi.mode(WIFI_STA);
        WiFi.begin(secrets_->wifiSsid, secrets_->wifiPass);
        state_ = State::Connecting;
        staDeadline_ = millis() + kStaTimeoutMs;
        Serial.printf("[WIFI] connecting to '%s'\n", secrets_->wifiSsid.c_str());
    }

    void becomeConnected() {
        if (WiFi.getMode() != WIFI_STA) {
            dns_.stop();
            WiFi.mode(WIFI_STA); // drop the recovery AP once STA works
        }
        state_ = State::Connected;
        Serial.printf("[WIFI] IP: %s RSSI: %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    void startProvisioning(const char* why) {
        state_ = State::Provisioning;
        // AP+STA so recovery keeps retrying the router (SPEC 81)
        WiFi.mode(secrets_->wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
        WiFi.softAP(apSsid_, apPass_);
        dns_.setErrorReplyCode(DNSReplyCode::NoError);
        dns_.start(53, "*", WiFi.softAPIP()); // captive portal (SPEC 80)
        nextRetryAt_ = millis() + kRetryIntervalMs;
        Serial.printf("[WIFI] PROVISIONING (%s): AP '%s' pass '%s' -> http://%s/\n",
                      why, apSsid_.c_str(), apPass_.c_str(),
                      WiFi.softAPIP().toString().c_str());
    }

    SecretsStore* secrets_ = nullptr;
    DNSServer dns_;
    State state_ = State::Connecting;
    String apSsid_, apPass_;
    uint32_t staDeadline_ = 0;
    uint32_t nextRetryAt_ = 0;
};
