#!/usr/bin/env python3
import base64
import os
import random
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

PROMPT_TIMEOUT = 15.0


def wait_for_port(port: int, timeout_s: float = 20.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"MUD did not start listening on port {port} within {timeout_s}s")


def unique_name(player_dir: Path) -> str:
    letters = "qzxjkvbpygfw"
    for _ in range(500):
        name = "".join(random.choice(letters) for _ in range(8))
        pfile = player_dir / name[0] / name.capitalize()
        if not pfile.exists():
            return name
    raise RuntimeError("Unable to generate unique test player name")


def ws_handshake(sock: socket.socket, host: str, port: int) -> None:
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    data = b""
    end = time.time() + PROMPT_TIMEOUT
    sock.settimeout(0.5)
    while time.time() < end:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        data += chunk
        if b"\r\n\r\n" in data:
            break
    text = data.decode(errors="ignore")
    if "101 Switching Protocols" not in text or "Sec-WebSocket-Accept" not in text:
        raise RuntimeError(f"Bad websocket handshake response: {text!r}")


def ws_send_text(sock: socket.socket, text: str) -> None:
    payload = text.encode()
    mask = os.urandom(4)
    frame = bytearray([0x81])
    n = len(payload)
    if n <= 125:
        frame.append(0x80 | n)
    elif n <= 0xFFFF:
        frame.append(0x80 | 126)
        frame.extend(struct.pack("!H", n))
    else:
        frame.append(0x80 | 127)
        frame.extend(struct.pack("!Q", n))
    frame.extend(mask)
    frame.extend(bytes(payload[i] ^ mask[i % 4] for i in range(n)))
    sock.sendall(frame)


def ws_recv_text(sock: socket.socket, timeout=PROMPT_TIMEOUT) -> str:
    end = time.time() + timeout
    sock.settimeout(0.5)
    while time.time() < end:
        try:
            h = sock.recv(2)
        except socket.timeout:
            continue
        if not h:
            raise RuntimeError("Socket closed")
        b1, b2 = h[0], h[1]
        opcode = b1 & 0x0F
        n = b2 & 0x7F
        if n == 126:
            n = struct.unpack("!H", sock.recv(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", sock.recv(8))[0]
        masked = (b2 & 0x80) != 0
        mask = sock.recv(4) if masked else b""
        payload = b""
        while len(payload) < n:
            payload += sock.recv(n - len(payload))
        if masked:
            payload = bytes(payload[i] ^ mask[i % 4] for i in range(n))
        if opcode == 0x1:
            return payload.decode(errors="ignore")
        if opcode == 0x9:
            continue
    raise RuntimeError("Timed out waiting for websocket text frame")


def recv_until(sock: socket.socket, needles) -> str:
    buf = ""
    low_needles = [n.lower() for n in needles]
    end = time.time() + PROMPT_TIMEOUT
    while time.time() < end:
        buf += ws_recv_text(sock)
        low = buf.lower()
        if any(n in low for n in low_needles):
            return buf
    raise RuntimeError(f"Timed out waiting for prompts {low_needles}; got tail: {buf[-300:]!r}")


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    area_dir = root / "area"
    player_dir = root / "player"
    merc_bin = root / "src" / "merc"

    port = random.randint(28001, 32000)
    mud = subprocess.Popen([str(merc_bin), str(port)], cwd=area_dir, stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    sock = None
    try:
        wait_for_port(port)
        sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        ws_handshake(sock, "127.0.0.1", port)

        recv_until(sock, ["by what name do you wish to be known", "enter your name please", "what is your name", "who do you think you are"])
        for _ in range(10):
            name = unique_name(player_dir)
            ws_send_text(sock, name)
            rsp = recv_until(sock, ["did i get that right", "password:", "illegal name", "name:"])
            low = rsp.lower()
            if "illegal name" in low:
                continue
            break
        else:
            raise RuntimeError("Could not find an acceptable new character name")

        password = "itestpw"
        ws_send_text(sock, "y")
        recv_until(sock, ["give me a password"])
        ws_send_text(sock, password)
        recv_until(sock, ["please retype password"])
        ws_send_text(sock, password)

        recv_until(sock, ["1)", "menu"])
        ws_send_text(sock, "1")
        recv_until(sock, ["sex", "m/f"])
        ws_send_text(sock, "m")

        recv_until(sock, ["1)", "menu"])
        ws_send_text(sock, "2")
        recv_until(sock, ["race"])
        ws_send_text(sock, "Ttn")

        recv_until(sock, ["1)", "menu"])
        ws_send_text(sock, "3")
        recv_until(sock, ["a)", "accept"])
        ws_send_text(sock, "a")

        recv_until(sock, ["1)", "menu"])
        ws_send_text(sock, "4")
        recv_until(sock, ["classes", "order"])
        ws_send_text(sock, "Cle War Mag Thi Psi")

        recv_until(sock, ["1)", "menu"])
        ws_send_text(sock, "5")
        recv_until(sock, ["welcome to", "may your visit", "type help", "this is very important"])

        time.sleep(8)
        ws_send_text(sock, "quit")
        return 0
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        mud.terminate()
        try:
            mud.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mud.kill()
            mud.wait(timeout=5)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"integration_websocket_test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
