#include "AlertsClient.h"
#include "AlertsCa.h"

// SPEC 133: production hostname is immutable; a mock endpoint exists only as
// a compile-time flag in dev builds and is unreachable from any UI.
#ifdef AIRALERT_MOCK_URL
static const char kAlertsUrl[] = AIRALERT_MOCK_URL;
#else
static const char kAlertsUrl[] = "https://api.alerts.in.ua/v1/alerts/active.json";
#endif

AlertsClient::Result AlertsClient::poll(airalert::SnapshotBuilder& builder) {
    Result r;

#ifdef AIRALERT_MOCK_URL
    WiFiClient client; // plain HTTP to the local mock only
#else
    BearSSL::WiFiClientSecure client;
    static BearSSL::X509List ca(ALERTS_CA_PEM);
    client.setTrustAnchors(&ca);
    client.setBufferSizes(4096, 512); // SPEC 113; RX must fit TLS records
    client.setSession(&session_);
#endif

    HTTPClient http;
    http.setTimeout(10000);
    http.useHTTP10(true); // no chunked encoding -> ArduinoJson can read the stream
    if (!http.begin(client, kAlertsUrl)) { r.kind = Result::Kind::NetError; return r; }

    http.addHeader("Authorization", "Bearer " + token_);
    if (lastModified_.length()) http.addHeader("If-Modified-Since", lastModified_);
    const char* keys[] = {"Last-Modified", "Retry-After"};
    http.collectHeaders(keys, 2);

    const int code = http.GET();
    r.httpCode = code;

    switch (code) {
        case HTTP_CODE_OK: {
            const String responseLastModified = http.header("Last-Modified");
            JsonDocument filter;
            airalert::buildAlertsFilter(filter);
            JsonDocument doc;
            const auto err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
            if (err != DeserializationError::Ok) {
                r.kind = Result::Kind::ParseError;
                break;
            }
            if (airalert::extractAlerts(doc, builder, r.stats)
                != airalert::ParseError::None) {
                r.kind = Result::Kind::ParseError; // SPEC 167
                break;
            }
            lastModified_ = responseLastModified;
            cache_.commitValidSnapshot();
            r.kind = Result::Kind::Ok;
            break;
        }
        case HTTP_CODE_NOT_MODIFIED:
            if (cache_.canAcceptNotModified()) {
                r.kind = Result::Kind::NotModified;
            } else {
                // A 304 without a validated snapshot cannot establish state.
                lastModified_ = "";
                r.kind = Result::Kind::ParseError;
            }
            break;
        case HTTP_CODE_UNAUTHORIZED: r.kind = Result::Kind::AuthError; break;   // SPEC 163
        case HTTP_CODE_FORBIDDEN: r.kind = Result::Kind::Forbidden; break;      // SPEC 164
        case HTTP_CODE_TOO_MANY_REQUESTS:                                        // SPEC 165
            r.kind = Result::Kind::RateLimited;
            r.retryAfterSec = http.header("Retry-After").toInt();
            break;
        default:
            r.kind = code < 0 ? Result::Kind::NetError : Result::Kind::HttpError;
            break;
    }
    http.end();
    return r;
}
