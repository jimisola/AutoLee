#!/usr/bin/env python3
"""Mock AutoLee web server — develop/preview the web UI without hardware.

Serves the real embedded UI (extracted from src/web_server.cpp) and fakes the
/api/v1/* endpoints and the /events SSE stream with schema-valid state
(seeded from api/schemas/state.example.json). Stdlib only.

Usage:
    python tools/mock_server.py         # http://localhost:8080
    python tools/mock_server.py 9000    # custom port
"""

import json
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
STATE_LOCK = threading.Lock()


def load_index_html() -> str:
    """Pull the INDEX_HTML raw string literal out of web_server.cpp."""
    src = (ROOT / "src" / "web_server.cpp").read_text(encoding="utf-8", errors="replace")
    m = re.search(
        r'INDEX_HTML\[\]\s*PROGMEM\s*=\s*R"rawliteral\((.*?)\)rawliteral"', src, re.DOTALL
    )
    if not m:
        return "<h1>Could not extract INDEX_HTML from web_server.cpp</h1>"
    return m.group(1)


def load_state() -> dict:
    return json.loads((ROOT / "api" / "schemas" / "state.example.json").read_text())


INDEX_HTML = load_index_html()
STATE = load_state()
LOG = ["mock server started"]


def apply_action(path: str, q: dict) -> None:
    """Mutate the mock state just enough to see the UI react."""
    with STATE_LOCK:
        if path == "/api/v1/toggle_run":
            STATE["state"] = "IDLE" if STATE["state"] == "RUNNING" else "RUNNING"
        elif path == "/api/v1/profile" and "idx" in q:
            idx = int(q["idx"][0])
            STATE["profileIdx"] = idx
            STATE["profileName"] = STATE["profiles"][idx]["name"]
            STATE["speed"] = STATE["profiles"][idx]["hz"]
            STATE["sgTrip"] = STATE["profiles"][idx]["sg"]
        elif path == "/api/v1/current" and "ma" in q:
            STATE["currentMa"] = int(q["ma"][0])
        elif path == "/api/v1/batch":
            if q.get("action", [""])[0] == "start":
                STATE["batchActive"] = True
                STATE["batchCount"] = 0
            elif q.get("action", [""])[0] == "clear":
                STATE["batchActive"] = False
            elif "delta" in q:
                STATE["batchTarget"] = max(0, STATE["batchTarget"] + int(q["delta"][0]))
        elif path == "/api/v1/action" and q.get("do", [""])[0] == "reset_counter":
            STATE["counter"] = 0
        LOG.append(f"POST {path} {dict((k, v[0]) for k, v in q.items())}")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):  # quiet
        pass

    def _send(self, code, body, ctype="text/plain"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ("/", "/index.html"):
            self._send(200, INDEX_HTML, "text/html; charset=utf-8")
        elif u.path == "/api/v1/state":
            with STATE_LOCK:
                self._send(200, json.dumps(STATE), "application/json")
        elif u.path == "/api/v1/events":
            self._sse()
        else:
            self._send(404, "not found")

    def do_POST(self):
        u = urlparse(self.path)
        apply_action(u.path, parse_qs(u.query))
        self._send(200, "ok")

    def _sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        last_log = 0
        try:
            while True:
                with STATE_LOCK:
                    payload = json.dumps(STATE)
                    new = LOG[last_log:]
                    last_log = len(LOG)
                self.wfile.write(f"data: {payload}\n\n".encode())
                if new:
                    self.wfile.write(f"event: log\ndata: {json.dumps({'log': new})}\n\n".encode())
                self.wfile.flush()
                time.sleep(0.25)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"AutoLee mock server on http://localhost:{port}  (Ctrl-C to stop)")
    ThreadingHTTPServer(("", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
