#!/usr/bin/env python3
"""Dev mock of api.alerts.in.ua (SPEC 143). Serves a fixture at
/v1/alerts/active.json with Last-Modified / If-Modified-Since / 304 support.

    python3 tools/mock_alerts_server.py --fixture test/fixtures/air_raid_oblast.json
    python3 tools/mock_alerts_server.py --status 429   # error simulation
"""
import argparse
import email.utils
import http.server
import os

ap = argparse.ArgumentParser()
ap.add_argument("--fixture", default="test/fixtures/no_alerts.json")
ap.add_argument("--port", type=int, default=8787)
ap.add_argument("--status", type=int, default=200, help="force this HTTP status")
args = ap.parse_args()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/v1/alerts/active.json":
            self.send_error(404)
            return
        if args.status != 200:
            self.send_response(args.status)
            if args.status == 429:
                self.send_header("Retry-After", "90")
            self.end_headers()
            return
        mtime = os.path.getmtime(args.fixture)
        last_mod = email.utils.formatdate(mtime, usegmt=True)
        if self.headers.get("If-Modified-Since") == last_mod:
            self.send_response(304)
            self.end_headers()
            return
        body = open(args.fixture, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Last-Modified", last_mod)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *a):  # quieter
        auth = "Bearer" in self.headers.get("Authorization", "")
        print(f"{self.command} {self.path} auth={auth} ims={'If-Modified-Since' in self.headers}")


print(f"mock alerts API on :{args.port}, fixture={args.fixture}, status={args.status}")
http.server.HTTPServer(("0.0.0.0", args.port), Handler).serve_forever()
