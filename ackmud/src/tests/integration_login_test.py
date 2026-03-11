#!/usr/bin/env python3
import random
import socket
import subprocess
import sys
import time
from pathlib import Path

PROMPT_TIMEOUT = 15.0


def recv_until(sock: socket.socket, needles, timeout=PROMPT_TIMEOUT) -> str:
    end = time.time() + timeout
    data = ""
    sock.settimeout(0.5)
    low_needles = [n.lower() for n in needles]
    while time.time() < end:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        data += chunk.decode(errors="ignore")
        low = data.lower()
        if any(n in low for n in low_needles):
            return data
    raise RuntimeError(f"Timed out waiting for prompts {low_needles}; got tail: {data[-300:]!r}")


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall((line + "\r\n").encode())


def unique_name(player_dir: Path) -> str:
    letters = "qzxjkvbpygfw"
    for _ in range(500):
        name = "".join(random.choice(letters) for _ in range(8))
        pfile = player_dir / name[0] / name.capitalize()
        if not pfile.exists():
            return name
    raise RuntimeError("Unable to generate unique test player name")


def wait_for_port(port: int, timeout_s: float = 20.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"MUD did not start listening on port {port} within {timeout_s}s")


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    area_dir = root / "area"
    player_dir = root / "player"
    merc_bin = root / "src" / "merc"

    port = random.randint(23000, 28000)
    mud = subprocess.Popen([str(merc_bin), str(port)], cwd=area_dir, stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    sock = None
    try:
        wait_for_port(port)
        sock = socket.create_connection(("127.0.0.1", port), timeout=3)

        recv_until(sock, ["by what name do you wish to be known", "enter your name please", "what is your name", "who do you think you are"])
        for _ in range(10):
            name = unique_name(player_dir)
            send_line(sock, name)
            rsp = recv_until(sock, ["did i get that right", "password:", "illegal name", "name:"])
            low = rsp.lower()
            if "illegal name" in low:
                continue
            break
        else:
            raise RuntimeError("Could not find an acceptable new character name")

        password = "itestpw"
        send_line(sock, "y")
        recv_until(sock, ["give me a password"])
        send_line(sock, password)
        recv_until(sock, ["please retype password"])
        send_line(sock, password)

        recv_until(sock, ["1)", "menu"])
        send_line(sock, "1")
        recv_until(sock, ["sex", "m/f"])
        send_line(sock, "m")

        recv_until(sock, ["1)", "menu"])
        send_line(sock, "2")
        recv_until(sock, ["race"])
        send_line(sock, "Ttn")

        recv_until(sock, ["1)", "menu"])
        send_line(sock, "3")
        recv_until(sock, ["a)", "accept"])
        send_line(sock, "a")

        recv_until(sock, ["1)", "menu"])
        send_line(sock, "4")
        recv_until(sock, ["classes", "order"])
        send_line(sock, "Cle War Mag Thi Psi")

        recv_until(sock, ["1)", "menu"])
        send_line(sock, "5")
        recv_until(sock, ["welcome to", "may your visit", "type help", "this is very important"])

        time.sleep(8)
        send_line(sock, "quit")
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
        print(f"integration_login_test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
