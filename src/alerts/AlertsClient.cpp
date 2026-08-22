#include "AlertsClient.h"
#include "AlertsCa.h"
#ifdef AIRALERT_DEV
#include "CaBundle.h"
#endif

// SPEC 133: production hostname is immutable. Dev builds may point at a local
// emulator at runtime (serial `setmock`), never via any web UI.
static const char kAlertsUrl[] = "https://api.alerts.in.ua/v1/alerts/active.json";

AlertsClient::Result AlertsClient::poll(airalert::SnapshotBuilder& builder) {
    Result r;

    const char* url = kAlertsUrl;
    BearSSL::WiFiClientSecure secureClient;
    WiFiClient* client = &secureClient;
#ifdef AIRALERT_DEV
    bool devMockTls = false;
    WiFiClient plainClient;
    if (mockUrl_.length()) {
        url = mockUrl_.c_str();
        if (mockUrl_.startsWith("https")) {
            devMockTls = true;
        } else {
            client = &plainClient;
        }
    }
#endif
    if (client == &secureClient) {
#ifdef AIRALERT_DEV
        if (devMockTls) {
            static BearSSL::X509List devCas(CA_BUNDLE_PEM);
            secureClient.setTrustAnchors(&devCas);
        } else
#endif
        {
            static BearSSL::X509List ca(ALERTS_CA_PEM);
            secureClient.setTrustAnchors(&ca);
            secureClient.setSession(&session_);
        }
        secureClient.setBufferSizes(4096, 512); // SPEC 113; RX must fit TLS records
    }

    HTTPClient http;
    http.setTimeout(10000);
    http.useHTTP10(true); // no chunked encoding -> ArduinoJson can read the stream
    if (!http.begin(*client, url)) { r.kind = Result::Kind::NetError; return r; }

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
