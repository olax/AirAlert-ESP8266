// Iso8601 / BackoffPolicy / ApiHealth — SPEC 8, 10, 30-31, 163, 165
#include <unity.h>
#include "airalert/Iso8601.h"
#include "airalert/BackoffPolicy.h"
#include "airalert/ApiHealth.h"

using namespace airalert;

void test_iso8601_known_values() {
    // python3: datetime(2022,4,4,16,45,39, tzinfo=utc).timestamp() == 1649090739
    TEST_ASSERT_EQUAL_INT64(1649090739, parseIso8601Utc("2022-04-04T16:45:39.000Z"));
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc("1970-01-01T00:00:00.000Z"));
    TEST_ASSERT_EQUAL_INT64(86400, parseIso8601Utc("1970-01-02T00:00:00Z"));
    // leap year day: 2024-02-29
    TEST_ASSERT_EQUAL_INT64(1709164800, parseIso8601Utc("2024-02-29T00:00:00.000Z"));
}
void test_iso8601_garbage() {
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc(nullptr));
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc(""));
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc("not-a-date"));
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc("2026-13-01T00:00:00Z"));
    TEST_ASSERT_EQUAL_INT64(0, parseIso8601Utc("2026-01-99T00:00:00Z"));
}

// jitter disabled (pct=0) for exact assertions
static BackoffPolicy::Config noJitter() {
    BackoffPolicy::Config c;
    c.jitterPct = 0;
    return c;
}

void test_backoff_success_is_poll_interval() {
    BackoffPolicy b(noJitter());
    TEST_ASSERT_EQUAL_UINT32(15000, b.next(BackoffPolicy::Outcome::Success, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(15000, b.next(BackoffPolicy::Outcome::NotModified, 0, 0));
}
void test_backoff_error_ladder() { // SPEC 10: 15 30 60 120 120...
    BackoffPolicy b(noJitter());
    auto e = BackoffPolicy::Outcome::NetError;
    TEST_ASSERT_EQUAL_UINT32(15000, b.next(e, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(30000, b.next(e, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(60000, b.next(e, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(120000, b.next(e, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(120000, b.next(e, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(15000, b.next(BackoffPolicy::Outcome::Success, 0, 0)); // reset
    TEST_ASSERT_EQUAL_UINT32(15000, b.next(e, 0, 0)); // ladder restarts
}
void test_backoff_rate_limited() { // SPEC 165
    BackoffPolicy b(noJitter());
    TEST_ASSERT_EQUAL_UINT32(60000, b.next(BackoffPolicy::Outcome::RateLimited, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(90000, b.next(BackoffPolicy::Outcome::RateLimited, 90, 0));
    TEST_ASSERT_EQUAL_UINT32(60000, b.next(BackoffPolicy::Outcome::RateLimited, 10, 0)); // min wins
}
void test_backoff_auth_error() { // SPEC 163: 5 min
    BackoffPolicy b(noJitter());
    TEST_ASSERT_EQUAL_UINT32(300000, b.next(BackoffPolicy::Outcome::AuthError, 0, 0));
}
void test_backoff_jitter_bounds() {
    BackoffPolicy b; // 10% jitter
    for (int r = 0; r <= 255; r += 51) {
        uint32_t d = b.next(BackoffPolicy::Outcome::Success, 0, static_cast<uint8_t>(r));
        TEST_ASSERT_TRUE(d >= 13500 && d <= 16500);
    }
}

void test_health_stale_threshold() { // SPEC 31
    ApiHealth h;
    TEST_ASSERT_FALSE(h.everContacted());
    TEST_ASSERT_TRUE(h.stale(0));
    h.onContact(1000);
    TEST_ASSERT_TRUE(h.online());
    TEST_ASSERT_FALSE(h.stale(1000 + 59999));
    TEST_ASSERT_TRUE(h.stale(1000 + 60000));
}
void test_health_failure_keeps_last_contact() { // Invariant 1
    ApiHealth h;
    h.onContact(1000);
    h.onFailure();
    TEST_ASSERT_FALSE(h.online());
    TEST_ASSERT_TRUE(h.everContacted());
    TEST_ASSERT_FALSE(h.stale(30000)); // data not stale yet, just offline
}
void test_health_wraparound() {
    ApiHealth h;
    h.onContact(0xFFFFFF00u);
    TEST_ASSERT_FALSE(h.stale(0xFFFFFF00u + 59999u)); // wraps
    TEST_ASSERT_TRUE(h.stale(0xFFFFFF00u + 60000u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_iso8601_known_values);
    RUN_TEST(test_iso8601_garbage);
    RUN_TEST(test_backoff_success_is_poll_interval);
    RUN_TEST(test_backoff_error_ladder);
    RUN_TEST(test_backoff_rate_limited);
    RUN_TEST(test_backoff_auth_error);
    RUN_TEST(test_backoff_jitter_bounds);
    RUN_TEST(test_health_stale_threshold);
    RUN_TEST(test_health_failure_keeps_last_contact);
    RUN_TEST(test_health_wraparound);
    return UNITY_END();
}
