// Integration scenarios over the full core chain (SPEC 204):
// engine + health + notify driven through realistic poll sequences.
#include <unity.h>
#include "airalert/AlertEngine.h"
#include "airalert/ApiHealth.h"
#include "airalert/NotificationEngine.h"
#include "airalert/StartupPolicy.h"

using namespace airalert;

struct Rig {
    AlertEngine engine;
    ApiHealth health;
    NotificationEngine notify;
    uint32_t now = 0;

    // one successful poll cycle
    void poll(Coverage air, uint32_t advanceMs = 15000) {
        now += advanceMs;
        health.onContact(now);
        AlertEngine::Snapshot s{};
        s.types[0] = {air, static_cast<uint8_t>(air != Coverage::None), 1000};
        const bool firstSync = !engine.synced();
        EngineEvent ev[8];
        const size_t n = engine.applySnapshot(s, ev, 8);
        for (size_t i = 0; i < n; ++i)
            notify.onEngineEvent(ev[i],
                                 firstSync ? NotificationEngine::StartupMode::Short
                                           : NotificationEngine::StartupMode::Normal,
                                 now);
    }
    void pollFails(uint32_t advanceMs = 15000) {
        now += advanceMs;
        health.onFailure();
        // no snapshot applied - engine state untouched (Invariant 1/6)
    }
    bool sirenAt(uint32_t offsetMs) { return notify.tick(now + offsetMs); }
};

void test_api_disappears_during_alarm() { // SPEC 204 #1, Invariant 1
    Rig r;
    r.poll(Coverage::None); // first sync, clean
    r.poll(Coverage::Full); // alarm starts
    TEST_ASSERT_TRUE(r.engine.anyActive());
    for (int i = 0; i < 20; ++i) r.pollFails(); // 5 minutes of API silence
    TEST_ASSERT_TRUE(r.engine.anyActive());     // alert NOT cleared
    TEST_ASSERT_TRUE(r.health.stale(r.now));    // but data marked stale
    TEST_ASSERT_FALSE(r.health.online());
}

void test_end_after_stale_recovery() { // SPEC 33, 204 #6
    Rig r;
    r.poll(Coverage::None);
    r.poll(Coverage::Full);
    for (int i = 0; i < 10; ++i) r.pollFails();
    // API returns showing clear: end still needs 2 confirmations
    r.poll(Coverage::None);
    TEST_ASSERT_TRUE(r.engine.anyActive());
    r.poll(Coverage::None);
    TEST_ASSERT_FALSE(r.engine.anyActive()); // END after recovery (end_after_recovery)
    TEST_ASSERT_FALSE(r.health.stale(r.now));
}

void test_two_simultaneous_alerts() { // SPEC 204 #4
    Rig r;
    r.poll(Coverage::None);
    r.now += 15000;
    r.health.onContact(r.now);
    AlertEngine::Snapshot s{};
    s.types[static_cast<int>(AlertType::AirRaid)] = {Coverage::Full, 1, 1000};
    s.types[static_cast<int>(AlertType::ArtilleryShelling)] = {Coverage::Full, 1, 2000};
    EngineEvent ev[8];
    const size_t n = r.engine.applySnapshot(s, ev, 8);
    TEST_ASSERT_EQUAL(2, n);
    for (size_t i = 0; i < n; ++i)
        r.notify.onEngineEvent(ev[i], NotificationEngine::StartupMode::Normal, r.now);
    // one relay: patterns play sequentially, artillery (prio 60) first
    TEST_ASSERT_TRUE(r.notify.tick(r.now));
    TEST_ASSERT_EQUAL(1, r.notify.queued()); // air_raid start still pending
}

void test_mute_flow_during_alarm() { // SPEC 204 #5, Invariant 4
    Rig r;
    r.poll(Coverage::None);
    r.poll(Coverage::Full);
    TEST_ASSERT_TRUE(r.notify.tick(r.now)); // siren on
    r.notify.muteShort(r.now);
    TEST_ASSERT_FALSE(r.notify.tick(r.now + 100));
    // alert stays active while muted; reminder cycles stay silent
    for (int i = 0; i < 8; ++i) {
        r.poll(Coverage::Full);
        TEST_ASSERT_FALSE(r.notify.tick(r.now));
    }
    TEST_ASSERT_TRUE(r.engine.anyActive());
    // end clears mute and voices the END pattern
    r.poll(Coverage::None);
    r.poll(Coverage::None);
    TEST_ASSERT_FALSE(r.notify.muted());
    TEST_ASSERT_TRUE(r.notify.tick(r.now));
}

void test_reboot_during_alarm_short_then_silent() { // SPEC 204 #3, 26-28
    // boot #1 into active alert -> Short notification, fingerprint persisted
    AlertEngine::Snapshot s{};
    s.types[0] = {Coverage::Full, 1, 5000};
    const uint32_t fp = StartupPolicy::fingerprint(s);
    TEST_ASSERT_TRUE(StartupPolicy::shouldNotify(fp, 0, 0, 100000, 300));
    // crash + boot #2 60 s later, same alert set -> silent
    TEST_ASSERT_FALSE(StartupPolicy::shouldNotify(fp, fp, 100000, 100060, 300));
    // boot #3 after cooldown -> short again
    TEST_ASSERT_TRUE(StartupPolicy::shouldNotify(fp, fp, 100000, 100301, 300));
    // different (escalated) alert set -> notify regardless of cooldown
    s.types[0].earliestStartedAt = 7777;
    TEST_ASSERT_TRUE(StartupPolicy::shouldNotify(StartupPolicy::fingerprint(s),
                                                 fp, 100000, 100060, 300));
}

void test_wifi_outage_is_just_stale_data() { // SPEC 204 #2, 129
    Rig r;
    r.poll(Coverage::None);
    r.poll(Coverage::Full);
    r.notify.tick(r.now); // start pattern begins
    for (int i = 0; i < 40; ++i) r.pollFails(); // 10 min offline
    // logical state: ACTIVE+STALE, but siren only per Pattern Engine (SPEC 129):
    // start pattern long finished, no reminders configured -> silence
    r.now += 60000;
    TEST_ASSERT_FALSE(r.notify.tick(r.now));
    TEST_ASSERT_TRUE(r.engine.anyActive());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_api_disappears_during_alarm);
    RUN_TEST(test_end_after_stale_recovery);
    RUN_TEST(test_two_simultaneous_alerts);
    RUN_TEST(test_mute_flow_during_alarm);
    RUN_TEST(test_reboot_during_alarm_short_then_silent);
    RUN_TEST(test_wifi_outage_is_just_stale_data);
    return UNITY_END();
}
