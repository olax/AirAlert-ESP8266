// Config round-trip / validation / migration + StartupPolicy + log format
#include <unity.h>
#include <string>
#include "airalert/Config.h"
#include "airalert/StartupPolicy.h"
#include "airalert/EventLog.h"

using namespace airalert;

void test_defaults_valid() {
    AppConfig c;
    TEST_ASSERT_EQUAL(ConfigError::None, validateConfig(c));
    TEST_ASSERT_EQUAL(2, c.selectedCount);
    TEST_ASSERT_EQUAL(15, c.pollIntervalSec);
    TEST_ASSERT_EQUAL(90, c.profiles[static_cast<int>(AlertType::Nuclear)].priority);
}

void test_roundtrip_preserves_everything() {
    AppConfig c;
    strncpy(c.deviceName, "Siren-Irpin", sizeof c.deviceName);
    c.pollIntervalSec = 20;
    c.partialSiren = false;
    c.relayActiveHigh = false;
    c.relayMaxOnMs = 45000;
    c.selectedCount = 1;
    c.selected[0] = {123, LocationType::Hromada, 14, 67};
    c.profiles[0].start = Pattern{true, 5000, 500, 2};
    c.profiles[0].reminderIntervalMs = 600000;

    JsonDocument d;
    configToJson(c, d);
    AppConfig r;
    configFromJson(d.as<JsonVariantConst>(), r);

    TEST_ASSERT_EQUAL_STRING("Siren-Irpin", r.deviceName);
    TEST_ASSERT_EQUAL(20, r.pollIntervalSec);
    TEST_ASSERT_FALSE(r.partialSiren);
    TEST_ASSERT_FALSE(r.relayActiveHigh);
    TEST_ASSERT_EQUAL_UINT32(45000, r.relayMaxOnMs);
    TEST_ASSERT_EQUAL(1, r.selectedCount);
    TEST_ASSERT_EQUAL(123, r.selected[0].uid);
    TEST_ASSERT_EQUAL(LocationType::Hromada, r.selected[0].type);
    TEST_ASSERT_EQUAL(67, r.selected[0].raionUid);
    TEST_ASSERT_EQUAL_UINT32(5000, r.profiles[0].start.onMs);
    TEST_ASSERT_EQUAL(2, r.profiles[0].start.repeat);
    TEST_ASSERT_EQUAL_UINT32(600000, r.profiles[0].reminderIntervalMs);
    TEST_ASSERT_EQUAL(ConfigError::None, validateConfig(r));
}

void test_partial_old_config_keeps_defaults() { // SPEC 90: N+1 reads N
    const char* old = R"({"schema":1,"alerts":{"poll_sec":30}})";
    JsonDocument d;
    TEST_ASSERT_TRUE(deserializeJson(d, old) == DeserializationError::Ok);
    AppConfig c;
    configFromJson(d.as<JsonVariantConst>(), c);
    TEST_ASSERT_EQUAL(30, c.pollIntervalSec);         // taken from file
    TEST_ASSERT_EQUAL(60, c.apiStaleAfterSec);        // default kept
    TEST_ASSERT_EQUAL(2, c.selectedCount);            // default locations kept
    TEST_ASSERT_TRUE(c.profiles[0].enabled);
}

void test_validation_rejects_bad_ranges() { // SPEC 7, 42, 155
    AppConfig c;
    c.pollIntervalSec = 5; // below API-safe minimum
    TEST_ASSERT_EQUAL(ConfigError::BadPollInterval, validateConfig(c));
    c = AppConfig{};
    c.relayMaxOnMs = 300000; // above hard cap 60 s
    TEST_ASSERT_EQUAL(ConfigError::BadRelayLimit, validateConfig(c));
    c = AppConfig{};
    c.profiles[2].start.onMs = 120000;
    TEST_ASSERT_EQUAL(ConfigError::BadPattern, validateConfig(c));
    c = AppConfig{};
    c.startConfirmations = 0;
    TEST_ASSERT_EQUAL(ConfigError::BadConfirmations, validateConfig(c));
}

static AlertEngine::Snapshot activeSnap(int64_t startedAt) {
    AlertEngine::Snapshot s{};
    s.types[0] = {Coverage::Full, 1, startedAt};
    return s;
}

void test_fingerprint_stability() {
    const uint32_t f1 = StartupPolicy::fingerprint(activeSnap(1000));
    TEST_ASSERT_EQUAL_UINT32(f1, StartupPolicy::fingerprint(activeSnap(1000)));
    TEST_ASSERT_NOT_EQUAL(f1, StartupPolicy::fingerprint(activeSnap(2000)));
    AlertEngine::Snapshot empty{};
    TEST_ASSERT_EQUAL_UINT32(0x811C9DC5u, StartupPolicy::fingerprint(empty));
}

void test_startup_policy_cooldown() { // SPEC 28
    const uint32_t fp = StartupPolicy::fingerprint(activeSnap(1000));
    // fresh alert set: notify
    TEST_ASSERT_TRUE(StartupPolicy::shouldNotify(fp, 0, 0, 5000, 300));
    // same set, 100 s after last notification: silent
    TEST_ASSERT_FALSE(StartupPolicy::shouldNotify(fp, fp, 5000, 5100, 300));
    // same set, cooldown passed: notify again
    TEST_ASSERT_TRUE(StartupPolicy::shouldNotify(fp, fp, 5000, 5301, 300));
    // same set, clock unknown: stay silent (crash loop cannot re-siren)
    TEST_ASSERT_FALSE(StartupPolicy::shouldNotify(fp, fp, 5000, 0, 300));
    // empty set: nothing to notify
    TEST_ASSERT_FALSE(StartupPolicy::shouldNotify(0x811C9DC5u, 0, 0, 5000, 300));
}

void test_log_record_format() { // SPEC 94
    char buf[160];
    size_t n = formatLogRecord(buf, sizeof buf, 1755763200, LogEvent::AlertStart,
                               "type=air_raid");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1755763200,\"event\":\"ALERT_START\",\"detail\":\"type=air_raid\"}\n", buf);
    n = formatLogRecord(buf, sizeof buf, 0, LogEvent::Boot, nullptr);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":0,\"event\":\"BOOT\"}\n", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_valid);
    RUN_TEST(test_roundtrip_preserves_everything);
    RUN_TEST(test_partial_old_config_keeps_defaults);
    RUN_TEST(test_validation_rejects_bad_ranges);
    RUN_TEST(test_fingerprint_stability);
    RUN_TEST(test_startup_policy_cooldown);
    RUN_TEST(test_log_record_format);
    return UNITY_END();
}
