#!/usr/bin/env python3
"""Report the XDJ-XZ runtime hook, receiver stats, and discovery answer."""

from __future__ import annotations

import argparse
import pathlib
import socket
import sys
import time

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_nand_dump import open_telnet


def command(shell: socket.socket, text: str, wait: float = 0.3) -> str:
    shell.sendall(text.encode("ascii") + b"\r\n")
    time.sleep(wait)
    chunks: list[bytes] = []
    shell.settimeout(0.2)
    while True:
        try:
            chunk = shell.recv(16384)
        except (TimeoutError, ConnectionResetError, socket.timeout):
            break
        if not chunk:
            break
        chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


def discover(timeout: float = 1.5) -> list[str]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)
    replies: list[str] = []
    try:
        for target in ("255.255.255.255", "169.254.255.255"):
            try:
                sock.sendto(b"VJDISCOVER", (target, 50006))
            except OSError:
                pass
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                data, addr = sock.recvfrom(512)
            except (socket.timeout, OSError):
                break
            replies.append(f"{addr[0]} -> {data.decode('ascii', 'replace').strip()}")
    finally:
        sock.close()
    return replies


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="169.254.168.59")
    args = parser.parse_args()

    print("== UDP discovery ==")
    replies = discover()
    print("\n".join(replies) if replies else "no discovery reply")

    print("\n== telnet runtime ==")
    shell = open_telnet(args.host)
    try:
        print(command(shell, "pid=$(pidof rbp); echo PID=$pid; grep -F directfb-hook /proc/$pid/maps"))
        print(command(shell, "ifconfig eth0 | head -3"))
        print(command(shell, "cat /tmp/xz_directfb_stats 2>/dev/null || echo NO_STATS"))
        print(command(shell, "tail -12 /tmp/xz_directfb_hook.log 2>/dev/null || echo NO_LOG"))
    finally:
        shell.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
