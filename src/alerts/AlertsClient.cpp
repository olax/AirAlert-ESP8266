#include "AlertsClient.h"
#include "AlertsCa.h"
#ifdef AIRALERT_DEV
#include "CaBundle.h"
#endif

// SPEC 133: production hostname is immutable. Dev builds may point at a local
// emulator at runtime (serial `setmock`), never via any web UI.
static const char kAlertsUrl[] = "https://api.alerts.in.ua/v1/alerts/active.json";

// Constant-memory body parse: alerts are deserialized ONE ELEMENT at a time,
// so heap use does not grow with the nationwide alert count. The previous
// whole-document parse hit DeserializationError::NoMemory on busy nights
// (36+ alerts with ~25 KB free heap while TLS buffers are live) and showed
// up as a permanent API_PARSE_ERROR streak.
AlertsClient::Result::Kind AlertsClient::parseBody(Stream& in,
                                                   airalert::SnapshotBuilder& builder,
                                                   Result& r) {
    // 1) scan to the "alerts" key and its '[' (first top-level key in practice)
    static const char kKey[] = "\"alerts\"";
    size_t ki = 0;
    bool inArray = false;
    const uint32_t deadline = millis() + 8000;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        const int c = in.read();
        if (c < 0) {
            if (!in.available()) { delay(1); continue; }
            continue;
        }
        if (!inArray) {
            if (ki < sizeof kKey - 1) {
                ki = (c == kKey[ki]) ? ki + 1 : (c == kKey[0] ? 1 : 0);
            } else if (c == '[') {
                inArray = true;
                break;
            }
        }
    }
    if (!inArray) {
        r.parseDetail = "NoAlertsArray";
        return Result::Kind::ParseError; // SPEC 167
    }

    builder.reset();
    JsonDocument filter;
    airalert::buildAlertElementFilter(filter);

    // 2) element loop: {..},{..}] — one small filtered doc per alert
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        int c = in.peek();
        if (c < 0) { delay(1); continue; }
        if (c == ']') { in.read(); return Result::Kind::Ok; }
        if (c == ',' || c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            in.read();
            continue;
        }
        JsonDocument doc;
        const auto err = deserializeJson(doc, in, DeserializationOption::Filter(filter));
        if (err != DeserializationError::Ok) {
            r.parseDetail = err.c_str(); // static string from ArduinoJson
            r.heapAtError = ESP.getFreeHeap();
            return Result::Kind::ParseError;
        }
        ++r.stats.total;
        const airalert::Alert a = airalert::alertFromJson(doc.as<JsonVariantConst>());
        if (a.locationUid == 0) ++r.stats.skipped;
        else builder.add(a);
        yield();
    }
    r.parseDetail = "timeout";
    return Result::Kind::ParseError;
}

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
        // MFLN probe (once): if the server honors 1 KB TLS fragments, the RX
        // buffer drops 4096->1024 and the handshake peak needs ~3 KB less -
        // that margin is the difference between polling and code=-1 on a
        // ~25 KB heap. Falls back to 4096 when unsupported.
        static int8_t mfln = -1; // -1 unknown, 0 no, 1 yes
        if (mfln < 0 && url == kAlertsUrl)
            mfln = BearSSL::WiFiClientSecure::probeMaxFragmentLength(
                       "api.alerts.in.ua", 443, 1024) ? 1 : 0;
        secureClient.setBufferSizes(mfln == 1 ? 1024 : 4096, 512); // SPEC 113
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
            r.kind = parseBody(http.getStream(), builder, r);
            if (r.kind != Result::Kind::Ok) break; // builder NOT committed (Invariant 6)
            lastModified_ = responseLastModified;
            cache_.commitValidSnapshot();
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
