import importlib.util
import pathlib
import threading
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "alerts_emulator", ROOT / "tools/alerts_emulator.py"
)
emulator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(emulator)


class AlertsEmulatorTests(unittest.TestCase):
    def setUp(self):
        with emulator.state_lock:
            emulator.alerts.clear()
            emulator.mode[0] = "ok"
            emulator.last_modified[0] = int(time.time())

    def test_last_modified_changes_for_commands_in_the_same_second(self):
        before = emulator.last_modified[0]
        emulator.touch()
        first = emulator.last_modified[0]
        emulator.touch()

        self.assertGreater(first, before)
        self.assertGreater(emulator.last_modified[0], first)

    def test_scenario_sleep_does_not_lock_api_state(self):
        worker = threading.Thread(target=emulator.do_cmd, args=("sleep 0.2",))
        worker.start()
        time.sleep(0.02)
        acquired = emulator.state_lock.acquire(timeout=0.05)
        if acquired:
            emulator.state_lock.release()
        worker.join()

        self.assertTrue(acquired)

    def test_request_log_never_exposes_token_characters(self):
        token = "secret-token-value"
        marker = emulator.token_marker(token)

        self.assertEqual("present", marker)
        self.assertNotIn("secret-token-value", marker)

    def test_bad_location_uid_returns_an_error_instead_of_raising(self):
        self.assertIn("must be an integer", emulator.do_cmd("start air_raid nope"))
        self.assertIn("must be an integer", emulator.do_cmd("stop air_raid nope"))


if __name__ == "__main__":
    unittest.main()


class UkraineAlarmShapeTests(unittest.TestCase):
    def setUp(self):
        with emulator.state_lock:
            emulator.alerts.clear()

    def test_levels_of_one_alert_merge_into_one_active_alert(self):
        emulator.do_cmd("start air_raid 75 yellow")
        emulator.do_cmd("start air_raid 75 red")
        emulator.do_cmd("start chemical 703")
        with emulator.state_lock:
            body = emulator.response_body()

        by_uid = {r["regionId"]: r for r in body}
        air = by_uid["75"]["activeAlerts"]
        self.assertEqual(1, len(air))
        self.assertEqual("AIR", air[0]["type"])
        self.assertEqual({"Yellow", "Red"},
                         {l["alertLevel"] for l in air[0]["activeAlertLevels"]})
        self.assertEqual("CHEMICAL", by_uid["703"]["activeAlerts"][0]["type"])
        self.assertEqual("Community", by_uid["703"]["regionType"])

    def test_stop_without_level_clears_every_level(self):
        emulator.do_cmd("start air_raid 75 yellow")
        emulator.do_cmd("start air_raid 75 red")
        self.assertIn("stopped", emulator.do_cmd("stop air_raid 75"))
        self.assertEqual({}, emulator.alerts)
