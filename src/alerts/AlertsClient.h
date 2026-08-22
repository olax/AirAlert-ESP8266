#pragma once
// HTTP/TLS transport for alerts.in.ua (SPEC 174: transport only, no policy).
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "airalert/AlertsParser.h"
#include "airalert/ApiSnapshotCache.h"
#include "airalert/SnapshotBuilder.h"

class AlertsClient {
public:
    struct Result {
        enum class Kind : uint8_t {
            Ok, NotModified, AuthError, Forbidden, RateLimited,
            HttpError, NetError, ParseError
        };
        Kind kind = Kind::NetError;
        int httpCode = 0;
        uint32_t retryAfterSec = 0;
        airalert::ParseStats stats;
    };

    void begin(const String& token) {
        if (token != token_) invalidateCache();
        token_ = token;
    }
    bool hasToken() const { return token_.length() > 0; }
    void invalidateCache() {
        lastModified_ = "";
        cache_.invalidate();
    }
    bool hasCachedSnapshot() const { return cache_.canAcceptNotModified(); }

#ifdef AIRALERT_DEV
    // Dev-only: poll a local/tunneled emulator instead of production.
    // Set via serial `setmock <url>`; production builds compile this out.
    void setMockUrl(const String& url) {
        if (url != mockUrl_) invalidateCache();
        mockUrl_ = url;
    }
    bool mocked() const { return mockUrl_.length() > 0; }
#endif

    // One poll: GET active.json, stream-parse into the builder.
    // The builder is only committed by the caller on Kind::Ok (SPEC 168).
    Result poll(airalert::SnapshotBuilder& builder);

private:
    String token_;
    String lastModified_;
#ifdef AIRALERT_DEV
    String mockUrl_;
#endif
    airalert::ApiSnapshotCache cache_;
    BearSSL::Session session_; // TLS resumption between 15 s polls
};
