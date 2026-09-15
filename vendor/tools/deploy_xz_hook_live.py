#!/usr/bin/env python3
"""Deploy a RAM-only DirectFB hook to a running XDJ-XZ diagnostic loader."""

from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import socket
import sys
import time

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_nand_dump import open_telnet


def command(shell: socket.socket, text: str, wait: float = 0.08) -> str:
    shell.sendall(text.encode("ascii") + b"\r\n")
    time.sleep(wait)
    chunks: list[bytes] = []
    shell.settimeout(0.1)
    while True:
        try:
            chunk = shell.recv(8192)
        except (TimeoutError, ConnectionResetError):
            break
        if not chunk:
            break
        chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("hook", type=pathlib.Path)
    parser.add_argument("--host", default="169.254.177.67")
    args = parser.parse_args()
    payload = args.hook.read_bytes()
    expected = hashlib.md5(payload).hexdigest()
    encoded = base64.b64encode(payload).decode("ascii")
    remote = "/dev/shm/libxz-directfb-hook.so"

    shell = open_telnet(args.host)
    try:
        command(shell, "rm -f /tmp/vjhook.b64 /tmp/vjhook.new")
        for offset in range(0, len(encoded), 512):
            command(shell, f"printf '%s' '{encoded[offset:offset + 512]}' >> /tmp/vjhook.b64", 0.015)
        output = command(shell, f"base64 -d /tmp/vjhook.b64 > /tmp/vjhook.new && chmod 755 /tmp/vjhook.new && md5sum /tmp/vjhook.new", 0.25)
        if expected not in output.lower():
            raise RuntimeError(f"on-device hook hash mismatch; expected {expected}: {output}")
        command(shell, f"mv -f /tmp/vjhook.new {remote}; sync; echo DEPLOYED {expected}", 0.2)
        restart = (
            "#!/bin/sh\n"
            "sleep 1\n"
            "killall -9 rbp 2>/dev/null\n"
            "sleep 1\n"
            f"cd /; LD_PRELOAD={remote} /root/pdj/rbp >/tmp/rbp-xzmod.log 2>&1 &\n"
            "sleep 5\n"
            f"ifconfig eth0 {args.host} netmask 255.255.0.0 up\n"
            f"case '{args.host}' in 169.254.*) route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 2>/dev/null || true ;; esac\n"
            "echo RESTART_COMPLETE >/tmp/vjhook-restart.status\n"
        )
        encoded_restart = base64.b64encode(restart.encode("ascii")).decode("ascii")
        command(shell, f"printf '%s' '{encoded_restart}' | base64 -d > /tmp/restart-vjhook.sh; chmod 755 /tmp/restart-vjhook.sh", 0.1)
        # This BusyBox image has no nohup. Keep the telnet shell alive until
        # the background restart script has relaunched rbp and restored eth0.
        command(shell, "sh /tmp/restart-vjhook.sh </dev/null >/tmp/restart-vjhook.log 2>&1 &", 8.0)
    finally:
        shell.close()
    verify_shell = None
    last_error: Exception | None = None
    for _ in range(15):
        time.sleep(1)
        try:
            verify_shell = open_telnet(args.host)
            break
        except OSError as error:
            last_error = error
    if verify_shell is None:
        raise RuntimeError(f"XDJ did not recover after hook restart: {last_error}")
    try:
        output = command(
            verify_shell,
            "pid=$(pidof rbp); echo PID=$pid; grep -F /dev/shm/libxz-directfb-hook.so /proc/$pid/maps; cat /tmp/vjhook-restart.status",
            0.4,
        )
        if "RESTART_COMPLETE" not in output or "/dev/shm/libxz-directfb-hook.so" not in output:
            raise RuntimeError(f"replacement rbp did not map the deployed hook: {output}")
    finally:
        verify_shell.close()
    print(f"deployed={remote} md5={expected} bytes={len(payload)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
