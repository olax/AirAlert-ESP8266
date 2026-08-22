import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class WebAssetContractTests(unittest.TestCase):
    def test_ndjson_is_read_as_text_and_errors_are_visible(self):
        page = (ROOT / "web/index.html").read_text()
        server = (ROOT / "src/web/WebUi.cpp").read_text()

        self.assertIn('ct.includes("application/json")', page)
        self.assertNotIn('ct.includes("json")', page)
        self.assertIn('Не вдалося завантажити журнал', page)
        self.assertIn('"application/x-ndjson"', server)
        self.assertIn('sendHeader("Cache-Control", "no-store")', server)

    def test_first_run_setup_requires_the_random_setup_key(self):
        page = (ROOT / "web/setup.html").read_text()
        server = (ROOT / "src/web/WebUi.cpp").read_text()

        self.assertIn('id="skey"', page)
        self.assertIn('setup_key:', page)
        self.assertIn('constantTimeEqual(setupKey, d_.wifi->apPass())', server)

    def test_waiting_screen_explains_missing_locations_and_api_errors(self):
        page = (ROOT / "web/index.html").read_text()

        self.assertIn('Оберіть щонайменше одну локацію', page)
        self.assertIn('API відхилив токен (401)', page)


if __name__ == "__main__":
    unittest.main()
