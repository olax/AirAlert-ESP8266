// AlertsParser + SnapshotBuilder over fixtures — SPEC 138, 142, 167-168
#include <unity.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "airalert/AlertsParser.h"

using namespace airalert;

static std::string readFixture(const char* name) {
    std::string path = std::string("test/fixtures/") + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { // when cwd differs, try one level up
        path = std::string("../") + path;
        f = fopen(path.c_str(), "rb");
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(f, name);
    std::string out;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Same fixture catalogue as test_matcher.
struct FixtureCatalog : ILocationCatalog {
    bool findByUid(uint16_t uid, Location& out) const override {
        switch (uid) {
            case 14: out = {14, LocationType::Oblast, 0, 0}; return true;
            case 67: out = {67, LocationType::Raion, 14, 0}; return true;
            case 123: out = {123, LocationType::Hromada, 14, 67}; return true;
            case 31: out = {31, LocationType::City, 0, 0}; return true;
        }
        return false;
    }
};
static FixtureCatalog cat;

static SnapshotBuilder makeBuilder(std::initializer_list<Location> sel) {
    SnapshotBuilder b;
    b.setCatalog(&cat);
    b.setSelected(sel.begin(), sel.size());
    return b;
}
static const Location SEL_HROMADA{123, LocationType::Hromada, 14, 67};
static const Location SEL_OBLAST{14, LocationType::Oblast, 0, 0};
static const Location SEL_KYIV{31, LocationType::City, 0, 0};

static ParseError run(const char* fixture, SnapshotBuilder& b, ParseStats& st) {
    std::string j = readFixture(fixture);
    return parseAlertsJson(j.data(), j.size(), b, st);
}

void test_no_alerts() {
    auto b = makeBuilder({SEL_HROMADA});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("no_alerts.json", b, st));
    TEST_ASSERT_EQUAL(0, st.total);
    TEST_ASSERT_EQUAL(Coverage::None,
        b.snapshot().types[static_cast<int>(AlertType::AirRaid)].coverage);
}

void test_oblast_alert_covers_selected_hromada() {
    auto b = makeBuilder({SEL_HROMADA});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("air_raid_oblast.json", b, st));
    const auto& t = b.snapshot().types[static_cast<int>(AlertType::AirRaid)];
    TEST_ASSERT_EQUAL(Coverage::Full, t.coverage);
    TEST_ASSERT_EQUAL_INT64(parseIso8601Utc("2026-08-21T08:00:00.000Z"), t.earliestStartedAt);
}

void test_hromada_alert_partial_for_selected_oblast() {
    auto b = makeBuilder({SEL_OBLAST});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("partial_alert.json", b, st));
    TEST_ASSERT_EQUAL(Coverage::Partial,
        b.snapshot().types[static_cast<int>(AlertType::AirRaid)].coverage);
}

void test_multiple_alerts_multi_select() {
    auto b = makeBuilder({SEL_HROMADA, SEL_KYIV});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("multiple_alerts.json", b, st));
    TEST_ASSERT_EQUAL(4, st.total); // raion entry repeats the inherited oblast alert
    const auto& air = b.snapshot().types[static_cast<int>(AlertType::AirRaid)];
    const auto& art = b.snapshot().types[static_cast<int>(AlertType::ArtilleryShelling)];
    TEST_ASSERT_EQUAL(Coverage::Full, air.coverage);   // oblast 14 covers hromada; Luhansk ignored
    TEST_ASSERT_EQUAL(1, air.locationCount);           // Kyiv city not covered
    TEST_ASSERT_EQUAL(Coverage::Full, art.coverage);   // raion 67 covers hromada
}

void test_chemical_and_nuclear_types() {
    auto b = makeBuilder({SEL_HROMADA, SEL_KYIV});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("chemical.json", b, st));
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.snapshot().types[static_cast<int>(AlertType::Chemical)].coverage);
    ParseStats st2;
    TEST_ASSERT_EQUAL(ParseError::None, run("nuclear.json", b, st2));
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.snapshot().types[static_cast<int>(AlertType::Nuclear)].coverage);
}

void test_unknown_type_no_crash() { // Invariant 9
    auto b = makeBuilder({SEL_OBLAST});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("unknown_type.json", b, st));
    TEST_ASSERT_EQUAL(2, st.total);
    TEST_ASSERT_EQUAL(1, st.skipped); // null regionId entry dropped
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.snapshot().types[static_cast<int>(AlertType::Unknown)].coverage);
}

void test_invalid_json_rejected() { // SPEC 167, Invariant 6
    auto b = makeBuilder({SEL_OBLAST});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::JsonInvalid, run("invalid.json", b, st));
}

void test_missing_alerts_array_rejected() { // SPEC 138: root must be the region array
    auto b = makeBuilder({SEL_OBLAST});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::NoAlertsArray, run("missing_alerts.json", b, st));
}

void test_uid_string_and_number_forms() {
    auto b = makeBuilder({SEL_OBLAST});
    ParseStats st;
    const char* j = R"([{"regionId":14,"activeAlerts":[
      {"regionId":14,"type":"AIR","lastUpdate":"2026-08-21T08:00:00Z",
       "activeAlertLevels":[{"alertLevel":"Red","createdAt":"2026-08-21T08:00:00.000Z"}]}]}])";
    TEST_ASSERT_EQUAL(ParseError::None, parseAlertsJson(j, strlen(j), b, st));
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.snapshot().types[static_cast<int>(AlertType::AirRaid)].coverage);
}

void test_per_location_details() { // dashboard breakdown (SPEC 71)
    auto b = makeBuilder({SEL_HROMADA, SEL_KYIV});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("multiple_alerts.json", b, st));
    // location 0 = hromada 123: air_raid Full (oblast 14), artillery Full (raion 67)
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.locCell(0, AlertType::AirRaid).coverage);
    TEST_ASSERT_EQUAL(Coverage::Full,
        b.locCell(0, AlertType::ArtilleryShelling).coverage);
    TEST_ASSERT_EQUAL_INT64(parseIso8601Utc("2026-08-21T08:00:00.000Z"),
        b.locCell(0, AlertType::AirRaid).startedAt);
    // location 1 = Kyiv city: nothing covers it
    TEST_ASSERT_EQUAL(Coverage::None,
        b.locCell(1, AlertType::AirRaid).coverage);
    TEST_ASSERT_EQUAL(31, b.selectedUid(1));
}

// ukrainealarm levels: yellow (drones) and red (missiles) are separate types
// with separate profiles. On ONE alert red dominates (raion 67 carries both
// levels -> red only); a yellow-only alert elsewhere (oblast 14) is yellow.
void test_yellow_and_red_levels_are_separate_types() {
    auto b = makeBuilder({SEL_HROMADA});
    ParseStats st;
    TEST_ASSERT_EQUAL(ParseError::None, run("levels.json", b, st));
    TEST_ASSERT_EQUAL(4, st.total);
    TEST_ASSERT_EQUAL(1, st.skipped); // INFO is a message, not a threat
    const auto& red = b.snapshot().types[static_cast<int>(AlertType::AirRaid)];
    const auto& yellow = b.snapshot().types[static_cast<int>(AlertType::AirRaidYellow)];
    TEST_ASSERT_EQUAL(Coverage::Full, red.coverage);    // raion 67 covers hromada 123
    TEST_ASSERT_EQUAL(Coverage::Full, yellow.coverage); // oblast 14 covers hromada 123
    TEST_ASSERT_EQUAL_INT64(parseIso8601Utc("2026-09-07T06:44:43Z"), red.earliestStartedAt);
    TEST_ASSERT_EQUAL_INT64(parseIso8601Utc("2026-09-07T07:10:00Z"), yellow.earliestStartedAt);
    TEST_ASSERT_EQUAL(1, yellow.locationCount);         // 67's yellow level was subsumed by its red
    // no activeAlertLevels at all = ungraded = red, start from lastUpdate
    const auto& art = b.snapshot().types[static_cast<int>(AlertType::ArtilleryShelling)];
    TEST_ASSERT_EQUAL(Coverage::Full, art.coverage);
    TEST_ASSERT_EQUAL_INT64(parseIso8601Utc("2026-09-07T05:00:00Z"), art.earliestStartedAt);
}

void test_unknown_level_counts_as_red() { // a siren errs on the loud side
    TEST_ASSERT_EQUAL(AlertType::AirRaid, alertTypeFromApi("AIR", "Orange"));
    TEST_ASSERT_EQUAL(AlertType::AirRaid, alertTypeFromApi("AIR", nullptr));
    TEST_ASSERT_EQUAL(AlertType::AirRaidYellow, alertTypeFromApi("AIR", "Yellow"));
    TEST_ASSERT_EQUAL(AlertType::Nuclear, alertTypeFromApi("NUCLEAR", "Yellow")); // levels only for AIR
    TEST_ASSERT_EQUAL(AlertType::Unknown, alertTypeFromApi("WHATEVER", "Red"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_alerts);
    RUN_TEST(test_oblast_alert_covers_selected_hromada);
    RUN_TEST(test_hromada_alert_partial_for_selected_oblast);
    RUN_TEST(test_multiple_alerts_multi_select);
    RUN_TEST(test_chemical_and_nuclear_types);
    RUN_TEST(test_unknown_type_no_crash);
    RUN_TEST(test_invalid_json_rejected);
    RUN_TEST(test_missing_alerts_array_rejected);
    RUN_TEST(test_uid_string_and_number_forms);
    RUN_TEST(test_per_location_details);
    RUN_TEST(test_yellow_and_red_levels_are_separate_types);
    RUN_TEST(test_unknown_level_counts_as_red);
    return UNITY_END();
}
