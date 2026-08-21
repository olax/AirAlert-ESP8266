// LocationMatcher tests — SPEC 135
#include <unity.h>
#include "airalert/LocationMatcher.h"

using namespace airalert;

// Tiny fixture catalogue:
//   oblast 14 (Київська)
//     raion 67 (Бучанський, oblast 14)
//       hromada 123 (Ірпінська, oblast 14, raion 67)
//     hromada 200 (інша громада, oblast 14, raion 68)
//   oblast 15 (інша область)
//     hromada 300 (oblast 15, raion 90)
//   city 31 (м. Київ, без parent)
struct FixtureCatalog : ILocationCatalog {
    bool findByUid(uint16_t uid, Location& out) const override {
        switch (uid) {
            case 14: out = {14, LocationType::Oblast, 0, 0}; return true;
            case 67: out = {67, LocationType::Raion, 14, 0}; return true;
            case 123: out = {123, LocationType::Hromada, 14, 67}; return true;
            case 200: out = {200, LocationType::Hromada, 14, 68}; return true;
            case 15: out = {15, LocationType::Oblast, 0, 0}; return true;
            case 300: out = {300, LocationType::Hromada, 15, 90}; return true;
            case 31: out = {31, LocationType::City, 0, 0}; return true;
        }
        return false;
    }
};

static FixtureCatalog cat;
static LocationMatcher m(&cat);

static Location HROMADA_IRPIN{123, LocationType::Hromada, 14, 67};
static Location OBLAST_KYIVSKA{14, LocationType::Oblast, 0, 0};
static Location RAION_BUCHA{67, LocationType::Raion, 14, 0};
static Location CITY_KYIV{31, LocationType::City, 0, 0};

static Alert mkAlert(uint16_t uid, uint16_t oblastUid = 0) {
    Alert a; a.id = 1; a.type = AlertType::AirRaid;
    a.locationUid = uid; a.oblastUid = oblastUid;
    return a;
}

void test_exact_uid() {
    TEST_ASSERT_EQUAL(Coverage::Full, m.match(HROMADA_IRPIN, mkAlert(123, 14)));
}
void test_selected_hromada_oblast_alert() { // SPEC 17
    TEST_ASSERT_EQUAL(Coverage::Full, m.match(HROMADA_IRPIN, mkAlert(14)));
}
void test_selected_hromada_raion_alert() { // SPEC 17
    TEST_ASSERT_EQUAL(Coverage::Full, m.match(HROMADA_IRPIN, mkAlert(67, 14)));
}
void test_selected_oblast_hromada_alert() { // SPEC 18
    TEST_ASSERT_EQUAL(Coverage::Partial, m.match(OBLAST_KYIVSKA, mkAlert(123, 14)));
}
void test_selected_raion_child_hromada() { // raion uid known only via catalogue
    TEST_ASSERT_EQUAL(Coverage::Partial, m.match(RAION_BUCHA, mkAlert(123, 14)));
}
void test_different_oblast() {
    TEST_ASSERT_EQUAL(Coverage::None, m.match(HROMADA_IRPIN, mkAlert(300, 15)));
    TEST_ASSERT_EQUAL(Coverage::None, m.match(OBLAST_KYIVSKA, mkAlert(15)));
}
void test_sibling_hromada_no_match() {
    TEST_ASSERT_EQUAL(Coverage::None, m.match(HROMADA_IRPIN, mkAlert(200, 14)));
}
void test_city_special_status() {
    TEST_ASSERT_EQUAL(Coverage::Full, m.match(CITY_KYIV, mkAlert(31)));
    TEST_ASSERT_EQUAL(Coverage::None, m.match(CITY_KYIV, mkAlert(14)));
}
void test_unknown_uid_falls_back_to_api_oblast() { // SPEC 135 "unknown UID"
    // uid 999 is not in the catalogue, but API says it is in oblast 14
    TEST_ASSERT_EQUAL(Coverage::Partial, m.match(OBLAST_KYIVSKA, mkAlert(999, 14)));
    TEST_ASSERT_EQUAL(Coverage::None, m.match(HROMADA_IRPIN, mkAlert(999, 14)));
}
void test_no_catalog_still_works() {
    LocationMatcher bare(nullptr);
    TEST_ASSERT_EQUAL(Coverage::Full, bare.match(HROMADA_IRPIN, mkAlert(14)));
    TEST_ASSERT_EQUAL(Coverage::Partial, bare.match(OBLAST_KYIVSKA, mkAlert(999, 14)));
}
void test_zero_uids_never_match() {
    Location empty{};
    TEST_ASSERT_EQUAL(Coverage::None, m.match(empty, mkAlert(14)));
    TEST_ASSERT_EQUAL(Coverage::None, m.match(HROMADA_IRPIN, mkAlert(0)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_exact_uid);
    RUN_TEST(test_selected_hromada_oblast_alert);
    RUN_TEST(test_selected_hromada_raion_alert);
    RUN_TEST(test_selected_oblast_hromada_alert);
    RUN_TEST(test_selected_raion_child_hromada);
    RUN_TEST(test_different_oblast);
    RUN_TEST(test_sibling_hromada_no_match);
    RUN_TEST(test_city_special_status);
    RUN_TEST(test_unknown_uid_falls_back_to_api_oblast);
    RUN_TEST(test_no_catalog_still_works);
    RUN_TEST(test_zero_uids_never_match);
    return UNITY_END();
}
