import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareBoundaryContractTests(unittest.TestCase):
    def test_http_validator_is_committed_only_after_a_valid_snapshot(self):
        source = (ROOT / "src/alerts/AlertsClient.cpp").read_text()

        # streaming per-element parser replaced whole-document extractAlerts
        # (constant memory vs nationwide alert count); the contract is the
        # same: the cache validator commits only after a fully valid body
        parsed = source.index("parseBody(http.getStream()")
        committed = source.index("lastModified_ = responseLastModified")
        self.assertLess(parsed, committed)
        self.assertIn("buildAlertElementFilter", source)
        self.assertIn("cache_.commitValidSnapshot()", source[committed:])
        self.assertNotIn("new BearSSL::WiFiClientSecure", source)
        self.assertNotIn("setInsecure", source)

    def test_not_modified_response_reapplies_the_cached_confirmation_sample(self):
        source = (ROOT / "src/main.cpp").read_text()
        start = source.index("case Kind::NotModified:")
        end = source.index("case Kind::AuthError:", start)

        self.assertIn("applyAndNotify(builder.snapshot(), false)", source[start:end])

    def test_location_refresh_invalidates_old_state_and_validator(self):
        source = (ROOT / "src/main.cpp").read_text()
        start = source.index("static void refreshLocations()")
        end = source.index("static void applyConfig()", start)
        body = source[start:end]

        self.assertIn("engine.reset()", body)
        self.assertIn("builder.reset()", body)
        self.assertIn("client.invalidateCache()", body)
        self.assertIn("nextPollAt = millis()", body)

    def test_failed_ota_resumes_the_main_loop(self):
        web = (ROOT / "src/web/WebUi.cpp").read_text()
        main = (ROOT / "src/main.cpp").read_text()

        self.assertIn("d_.finishFailedOta()", web)
        self.assertIn("otaInProgress = false", main)

    def test_relay_deadline_uses_an_independent_timer_callback(self):
        source = (ROOT / "src/hardware/RelayController.h").read_text()

        self.assertIn("safetyTimer_.once_ms", source)
        self.assertIn("void emergencyOff()", source)
        self.assertIn("digitalWrite(pin_", source[source.index("void emergencyOff()"):])


if __name__ == "__main__":
    unittest.main()
