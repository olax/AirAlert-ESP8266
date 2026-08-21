#pragma once
// HTTP/TLS transport for alerts.in.ua (SPEC 174: transport only, no policy).
#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "airalert/AlertsParser.h"
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

    void begin(const String& token) { token_ = token; }
    bool hasToken() const { return token_.length() > 0; }

    // One poll: GET active.json, stream-parse into the builder.
    // The builder is only committed by the caller on Kind::Ok (SPEC 168).
    Result poll(airalert::SnapshotBuilder& builder);

private:
    String token_;
    String lastModified_;
    BearSSL::Session session_; // TLS resumption between 15 s polls
};
