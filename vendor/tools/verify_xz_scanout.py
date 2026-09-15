#!/usr/bin/env python3
"""Stream a deterministic overlay and inspect XDJ framebuffer memory repeatedly."""

from __future__ import annotations

import pathlib
import socket
import struct
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.capture_xz_framebuffer import PAGE_BYTES, capture

HEADER = struct.Struct("<4sBBHIHHHHIf")
CYAN = b"\xff\x07"


def stream(host: str, stop: threading.Event) -> None:
    width, height, top = 800, 120, 176
    pixels = CYAN * (width * height)
    with socket.create_connection((host, 50005), timeout=2) as client:
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sequence = 0
        deadline = time.perf_counter()
        while not stop.is_set():
            packet = HEADER.pack(
                b"VJFS", 1, 1, 1, sequence, width, height, 0, top, len(pixels), 0.0
            ) + pixels
            client.sendall(packet)
            sequence += 1
            deadline += 1 / 30
            time.sleep(max(0, deadline - time.perf_counter()))


def cyan_fraction(page: bytes) -> float:
    width, top, height = 800, 176, 120
    band = b"".join(
        page[(row * width * 2):((row + 1) * width * 2)]
        for row in range(top, top + height)
    )
    matches = sum(1 for offset in range(0, len(band), 2) if band[offset:offset + 2] == CYAN)
    return matches / (width * height)


def main() -> int:
    host = "169.254.168.59"
    pc_host = "169.254.168.58"
    output = ROOT / "build" / "scanout-proof"
    output.mkdir(parents=True, exist_ok=True)
    stop = threading.Event()
    sender = threading.Thread(target=stream, args=(host, stop), daemon=True)
    sender.start()
    time.sleep(1)
    results: list[tuple[str, list[float]]] = []
    try:
        for index in range(3):
            for device, port in (("/dev/fb0", 4260 + index),):
                target = output / f"capture-{index}-{device[-3:]}.png"
                capture(host, pc_host, target, port, device)
                raw = target.with_suffix(".rgb565").read_bytes()
                pages = [raw[offset:offset + PAGE_BYTES] for offset in range(0, len(raw), PAGE_BYTES)]
                results.append((f"{index}:{device}", [cyan_fraction(page) for page in pages]))
            time.sleep(0.25)
    finally:
        stop.set()
        sender.join(timeout=2)
    for label, fractions in results:
        print(label, " ".join(f"page{index}={fraction:.4f}" for index, fraction in enumerate(fractions)))
    stable = any(all(fraction > 0.98 for fraction in fractions) for _, fractions in results)
    print(f"{'PASS' if stable else 'FAIL'} stable_cyan_scanout={stable}")
    return 0 if stable else 1


if __name__ == "__main__":
    raise SystemExit(main())
