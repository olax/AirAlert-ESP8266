#!/usr/bin/env python3
"""alerts.in.ua emulator for AirAlert dynamic testing (supersedes the static
mock, SPEC 143). Stdlib only - runs on Windows `py -3` or Linux python3.

  python3 tools/alerts_emulator.py [--port 8787] [--log requests.jsonl]
                                   [--scenario tools/scenarios/demo.txt]
                                   [--require-token]

Data endpoint (what the firmware polls):
  GET /v1/alerts/active.json   - real API shape, Last-Modified + 304

Control (curl or browser, also available as stdin REPL):
  GET /ctl?cmd=start+air_raid+14      - start an alert (oblast 14)
  GET /ctl?cmd=start+chemical+10123+14 - hromada inside oblast 14 (uid >= 10000!)
  GET /ctl?cmd=stop+air_raid+14
  GET /ctl?cmd=clear
  GET /ctl?cmd=mode+429               - ok|401|403|429|500|invalid|slow
  GET /ctl?cmd=status
  GET /log                            - recent request log (JSONL)

Scenario file: one command per line, plus `sleep N`; `#` comments.
"""
import argparse
import io as _io
import email.utils
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

args = argparse.Namespace(log="", require_token=False)

TYPES = {"air_raid", "artillery_shelling", "urban_fights", "chemical", "nuclear"}
# NB: hromada UIDs are NOT in the official catalogue, so test hromadas must use
# uids ABOVE the official range (10000+). A low fake uid (e.g. 123) collides
# with a real raion in another oblast and the firmware will rightly not match.
LOCATION_NAMES = {14: "Київська область", 31: "м. Київ", 16: "Луганська область",
                  67: "Бучанський район", 10123: "Ірпінська громада (тест)"}

state_lock = threading.Lock()
log_lock = threading.Lock()
alerts = {}          # (type, uid) -> alert dict
next_id = [1000]
mode = ["ok"]
last_modified = [int(time.time())]
reqlog = []          # ring buffer of request records
stats = {"requests": 0, "200": 0, "304": 0, "errors": 0}


def now_iso(ts=None):
    return time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime(ts or time.time()))


def touch():
    # HTTP dates have one-second resolution. Ensure two quick commands still
    # produce different validators so the firmware cannot receive a false 304.
    last_modified[0] = max(int(time.time()), last_modified[0] + 1)


def do_cmd(line: str) -> str:
    p = line.strip().split()
    if not p or p[0].startswith("#"):
        return ""
    cmd = p[0]
    if cmd == "sleep" and len(p) >= 2:
        try:
            delay = float(p[1])
        except ValueError:
            return "sleep value must be a number"
        if delay < 0:
            return "sleep value must be non-negative"
        time.sleep(delay)  # never hold state_lock while a scenario is waiting
        return f"slept {p[1]}s"
    with state_lock:
        if cmd == "start" and len(p) >= 3:
            typ = p[1]
            try:
                uid = int(p[2])
                oblast = int(p[3]) if len(p) > 3 else uid
            except ValueError:
                return "uid and oblast uid must be integers"
            if typ not in TYPES:
                return f"unknown type {typ} (known: {', '.join(sorted(TYPES))})"
            ltype = "oblast" if uid == oblast and uid != 31 else \
                    ("city" if uid in (30, 31) else "hromada")
            next_id[0] += 1
            alerts[(typ, uid)] = {
                "id": next_id[0],
                "location_title": LOCATION_NAMES.get(uid, f"Локація {uid}"),
                "location_type": ltype,
                "started_at": now_iso(),
                "finished_at": None,
                "updated_at": now_iso(),
                "alert_type": typ,
                "location_uid": str(uid),
                "location_oblast": LOCATION_NAMES.get(oblast, f"Область {oblast}"),
                "location_oblast_uid": str(oblast),
                "location_raion": None,
                "notes": "емулятор",
                "calculated": False,
            }
            touch()
            return f"started {typ} @ {uid}"
        if cmd == "stop" and len(p) >= 3:
            try:
                uid = int(p[2])
            except ValueError:
                return "uid must be an integer"
            if alerts.pop((p[1], uid), None):
                touch()
                return f"stopped {p[1]} @ {p[2]}"
            return "no such alert"
        if cmd == "clear":
            alerts.clear()
            touch()
            return "cleared"
        if cmd == "mode" and len(p) >= 2:
            if p[1] not in ("ok", "401", "403", "429", "500", "invalid", "slow"):
                return "modes: ok 401 403 429 500 invalid slow"
            mode[0] = p[1]
            return f"mode={p[1]}"
        if cmd == "status":
            act = ", ".join(f"{t}@{u}" for t, u in alerts) or "немає"
            with log_lock:
                counters = stats.copy()
            return (f"alerts: {act} | mode={mode[0]} | requests={counters['requests']} "
                    f"(200:{counters['200']} 304:{counters['304']} "
                    f"err:{counters['errors']})")
    return f"? {line} (start/stop/clear/mode/status/sleep)"


def log_request(rec):
    rec["t"] = now_iso()
    with log_lock:
        reqlog.append(rec)
        del reqlog[:-500]
    tok = rec.get("token", "")
    print(f"[{rec['t']}] {rec['code']} {rec.get('src','?')} {rec['path']}"
          f"{' ims' if rec.get('ims') else ''}"
          f"{' token=' + tok if tok else ' NO-TOKEN'}"
          f" {rec.get('note', '')}".rstrip(), flush=True)
    if args.log:
        with log_lock:
            with open(args.log, "a", encoding="utf-8") as f:
                f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def token_marker(authorization: str) -> str:
    return "present" if authorization.startswith("Bearer ") else ""


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _send(self, code, body=b"", ctype="application/json", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/v1/alerts/active.json":
            return self.serve_alerts()
        if u.path == "/ctl":
            cmd = (parse_qs(u.query).get("cmd") or [""])[0]
            out = do_cmd(cmd)
            print(f"[ctl] {cmd} -> {out}", flush=True)
            return self._send(200, (out + "\n").encode())
        if u.path == "/log":
            with log_lock:
                records = list(reqlog)
            body = "\n".join(json.dumps(r, ensure_ascii=False) for r in records)
            return self._send(200, body.encode(), "application/x-ndjson")
        self._send(404, b'{"error":"not found"}')

    def serve_alerts(self):
        with log_lock:
            stats["requests"] += 1
        auth = self.headers.get("Authorization", "")
        token = token_marker(auth)
        rec = {"path": "/v1/alerts/active.json", "src": self.client_address[0], "token": token,
               "ims": "If-Modified-Since" in self.headers, "mode": mode[0]}

        m = mode[0]
        if args.require_token and not auth.startswith("Bearer "):
            m = "401"
        if m == "slow":
            time.sleep(15)  # longer than the firmware's 10 s timeout
            m = "ok"
        if m in ("401", "403", "500"):
            with log_lock:
                stats["errors"] += 1
            rec.update(code=int(m))
            log_request(rec)
            return self._send(int(m), b'{"message":"emulated error"}')
        if m == "429":
            with log_lock:
                stats["errors"] += 1
            rec.update(code=429, note="retry-after=90")
            log_request(rec)
            return self._send(429, b'{"message":"too many requests"}',
                              extra=[("Retry-After", "90")])
        if m == "invalid":
            with log_lock:
                stats["errors"] += 1
            rec.update(code=200, note="INVALID JSON")
            log_request(rec)
            return self._send(200, b'{"alerts":[{"id":broken')

        with state_lock:
            lm = email.utils.formatdate(last_modified[0], usegmt=True)
            body = json.dumps({"alerts": list(alerts.values())},
                              ensure_ascii=False).encode()
        if self.headers.get("If-Modified-Since") == lm:
            with log_lock:
                stats["304"] += 1
            rec.update(code=304)
            log_request(rec)
            return self._send(304, extra=[("Last-Modified", lm)])
        with log_lock:
            stats["200"] += 1
        rec.update(code=200, note=f"alerts={len(alerts)}")
        log_request(rec)
        self._send(200, body, extra=[("Last-Modified", lm)])

    def log_message(self, *a):  # our own logging instead
        pass


def repl():
    for line in sys.stdin:
        out = do_cmd(line)
        if out:
            print(out, flush=True)


def scenario(path):
    print(f"[scenario] running {path}", flush=True)
    for line in open(path, encoding="utf-8"):
        out = do_cmd(line)
        if out:
            print(f"[scenario] {line.strip()} -> {out}", flush=True)
    print("[scenario] done", flush=True)


def main():
    global args
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--log", default="", help="append request log (JSONL) to this file")
    ap.add_argument("--scenario", default="", help="run commands from file on start")
    ap.add_argument("--require-token", action="store_true",
                    help="respond 401 unless Authorization: Bearer is present")
    args = ap.parse_args()

    # Windows consoles default to cp1252 and choke on Ukrainian text.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    srv = ThreadingHTTPServer(("0.0.0.0", args.port), H)
    print(f"alerts emulator on :{args.port} | ctl: "
          f"curl 'http://HOST:{args.port}/ctl?cmd=start+air_raid+14'", flush=True)
    threading.Thread(target=repl, daemon=True).start()
    if args.scenario:
        threading.Thread(target=scenario, args=(args.scenario,), daemon=True).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
