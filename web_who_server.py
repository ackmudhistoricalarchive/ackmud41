#!/usr/bin/env python3
"""Lightweight web MUD gateway for ACK worlds.

Serves the web client and proxies commands/output over HTTP polling.
"""

from __future__ import annotations

import argparse
import json
import secrets
import socket
import threading
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Dict
from urllib.parse import urlparse

WEB_ROOT = Path(__file__).resolve().parent / "web"

WORLDS = {
    "ackmud.com:8890": ("ackmud.com", 8890),
    "ackmud.com:8891": ("ackmud.com", 8891),
    "ackmud.com:8892": ("ackmud.com", 8892),
}


@dataclass
class Session:
    sock: socket.socket
    lock: threading.Lock
    last_seen: float


SESSIONS: Dict[str, Session] = {}
SESSIONS_LOCK = threading.Lock()
SESSION_TTL_SECONDS = 60 * 20


def cleanup_sessions() -> None:
    while True:
        now = time.time()
        expired = []
        with SESSIONS_LOCK:
            for sid, sess in SESSIONS.items():
                if now - sess.last_seen > SESSION_TTL_SECONDS:
                    expired.append(sid)
            for sid in expired:
                sess = SESSIONS.pop(sid)
                try:
                    sess.sock.close()
                except OSError:
                    pass
        time.sleep(30)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB_ROOT), **kwargs)

    def _json(self, payload: dict, code: int = 200) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b"{}"
        return json.loads(raw or b"{}")

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/mud-client", "/mud-client/"):
            self.path = "/mud-client.html"
            return super().do_GET()

        if parsed.path == "/api/worlds":
            worlds = [
                {"name": "ACK!TNG", "target": "ackmud.com:8890"},
                {"name": "ACK!4.3.1", "target": "ackmud.com:8891"},
                {"name": "ACK!4.2", "target": "ackmud.com:8892"},
            ]
            return self._json({"worlds": worlds})

        if parsed.path == "/api/poll":
            sid = self.headers.get("X-Session-Id", "")
            with SESSIONS_LOCK:
                sess = SESSIONS.get(sid)
            if not sess:
                return self._json({"error": "invalid session"}, 401)

            sess.last_seen = time.time()
            data = []
            with sess.lock:
                sess.sock.setblocking(False)
                try:
                    while True:
                        chunk = sess.sock.recv(4096)
                        if not chunk:
                            break
                        data.append(chunk)
                        if len(chunk) < 4096:
                            break
                except BlockingIOError:
                    pass
                except OSError:
                    return self._json({"error": "connection closed"}, 410)

            return self._json({"output": b"".join(data).decode("utf-8", errors="replace")})

        return super().do_GET()

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)

        if parsed.path == "/api/connect":
            try:
                payload = self._read_json()
            except json.JSONDecodeError:
                return self._json({"error": "invalid json"}, 400)

            target = payload.get("target", "")
            if target not in WORLDS:
                return self._json({"error": "unknown world"}, 400)

            host, port = WORLDS[target]
            try:
                sock = socket.create_connection((host, port), timeout=8)
                sock.settimeout(1)
            except OSError as exc:
                return self._json({"error": f"connect failed: {exc}"}, 502)

            sid = secrets.token_urlsafe(24)
            with SESSIONS_LOCK:
                SESSIONS[sid] = Session(sock=sock, lock=threading.Lock(), last_seen=time.time())
            return self._json({"sessionId": sid})

        if parsed.path == "/api/send":
            try:
                payload = self._read_json()
            except json.JSONDecodeError:
                return self._json({"error": "invalid json"}, 400)

            sid = self.headers.get("X-Session-Id", "")
            cmd = payload.get("command", "")
            with SESSIONS_LOCK:
                sess = SESSIONS.get(sid)
            if not sess:
                return self._json({"error": "invalid session"}, 401)

            if not isinstance(cmd, str):
                return self._json({"error": "command must be a string"}, 400)

            sess.last_seen = time.time()
            try:
                with sess.lock:
                    sess.sock.sendall((cmd + "\n").encode("utf-8"))
            except OSError:
                return self._json({"error": "connection closed"}, 410)

            return self._json({"ok": True})

        if parsed.path == "/api/disconnect":
            sid = self.headers.get("X-Session-Id", "")
            with SESSIONS_LOCK:
                sess = SESSIONS.pop(sid, None)
            if sess:
                try:
                    sess.sock.close()
                except OSError:
                    pass
            return self._json({"ok": True})

        return self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run ACK web MUD gateway server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    cleaner = threading.Thread(target=cleanup_sessions, daemon=True)
    cleaner.start()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Serving mud client on http://{args.host}:{args.port}/mud-client")
    server.serve_forever()


if __name__ == "__main__":
    main()
