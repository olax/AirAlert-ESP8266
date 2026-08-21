#pragma once
// /secrets.json - Wi-Fi, API token, admin password hash (SPEC 91, 176).
// Values never leave the device; REST exposes only presence flags.
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <bearssl/bearssl_hash.h>

struct SecretsStore {
    String wifiSsid, wifiPass, apiToken;
    String webPassHash, webSalt; // hex(sha256(salt+password)), hex salt

    bool load() {
        File f = LittleFS.open("/secrets.json", "r");
        if (!f) return false;
        JsonDocument d;
        if (deserializeJson(d, f) != DeserializationError::Ok) return false;
        wifiSsid = d["wifi_ssid"] | "";
        wifiPass = d["wifi_pass"] | "";
        apiToken = d["api_token"] | "";
        webPassHash = d["web_pass_hash"] | "";
        webSalt = d["web_salt"] | "";
        return wifiSsid.length() > 0;
    }

    bool save() {
        JsonDocument d;
        d["wifi_ssid"] = wifiSsid;
        d["wifi_pass"] = wifiPass;
        d["api_token"] = apiToken;
        d["web_pass_hash"] = webPassHash;
        d["web_salt"] = webSalt;
        File f = LittleFS.open("/secrets.json", "w");
        if (!f) return false;
        serializeJson(d, f);
        f.close();
        return true;
    }

    static String sha256Hex(const String& in) {
        br_sha256_context ctx;
        uint8_t digest[32];
        br_sha256_init(&ctx);
        br_sha256_update(&ctx, in.c_str(), in.length());
        br_sha256_out(&ctx, digest);
        String out;
        out.reserve(64);
        for (uint8_t b : digest) {
            char h[3];
            snprintf(h, 3, "%02x", b);
            out += h;
        }
        return out;
    }

    void setWebPassword(const String& pass) {
        char salt[17];
        snprintf(salt, sizeof salt, "%08x%08x",
                 static_cast<unsigned>(ESP.random()), static_cast<unsigned>(ESP.random()));
        webSalt = salt;
        webPassHash = sha256Hex(webSalt + pass); // no plaintext stored (SPEC 100)
    }

    bool checkWebPassword(const String& pass) const {
        if (!webPassHash.length()) return false;
        return sha256Hex(webSalt + pass) == webPassHash;
    }

    bool hasWebPassword() const { return webPassHash.length() > 0; }
};
