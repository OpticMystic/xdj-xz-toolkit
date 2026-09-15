#!/usr/bin/env python3
"""Drive the real XDJ-XZ layered framebuffer receiver and assert its cadence."""

from __future__ import annotations

import argparse
import math
import re
import socket
import struct
import time


HEADER = struct.Struct("<4sBBHIHHHHIf")
STATS_RE = re.compile(r"([a-z_]+)=([0-9.]+)")


def header(
    magic: bytes,
    sequence: int,
    width: int,
    height: int,
    x: int,
    y: int,
    payload_length: int,
    playhead: float = 0.0,
    flags: int = 1,
    version: int = 1,
) -> bytes:
    return HEADER.pack(magic, version, 1, flags, sequence, width, height, x, y, payload_length, playhead)


def rgb565(red: int, green: int, blue: int) -> bytes:
    value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
    return struct.pack("<H", value)


def base_frame() -> bytes:
    width, height = 800, 480
    row = bytearray()
    for x in range(width):
        if x < 148 or x >= 652:
            row.extend(rgb565(10, 9, 8))
        else:
            row.extend(rgb565(5, 7, 9))
    pixels = bytes(row) * height
    return header(b"VJFS", 1, width, height, 0, 0, len(pixels)) + pixels


def filmstrip_asset(width: int = 1600, height: int = 120) -> bytes:
    pixels = bytearray(width * height * 2)
    for y in range(height):
        for x in range(width):
            stripe = (x // 40) % 2
            value = rgb565(12 + stripe * 8, 38 + stripe * 22, 58 + stripe * 24)
            offset = (y * width + x) * 2
            pixels[offset : offset + 2] = value
    return header(b"VJFA", 2, width, height, 0, 360, len(pixels)) + pixels


def preview_payload(width: int, height: int, phase: int, rgb332: bool = False) -> bytes:
    pixels = bytearray(width * height * (1 if rgb332 else 2))
    for y in range(height):
        green = int(255 * y / max(1, height - 1))
        for x in range(width):
            red = (x * 255 // max(1, width - 1) + phase * 3) & 0xFF
            blue = 255 - red
            if rgb332:
                pixels[y * width + x] = (red & 0xE0) | ((green >> 3) & 0x1C) | (blue >> 6)
            else:
                value = rgb565(red, green, blue)
                offset = (y * width + x) * 2
                pixels[offset : offset + 2] = value
    return bytes(pixels)


def read_stats(host: str, port: int) -> str:
    with socket.create_connection((host, port), timeout=2.0) as client:
        client.settimeout(1.0)
        time.sleep(0.15)
        client.sendall(b"cat /tmp/xz_directfb_stats\r\n")
        time.sleep(0.35)
        chunks: list[bytes] = []
        while True:
            try:
                chunk = client.recv(4096)
            except TimeoutError:
                break
            if not chunk:
                break
            chunks.append(chunk)
            if b"scroll=" in chunk:
                break
    text = b"".join(chunks).decode("ascii", errors="ignore")
    lines = [line.strip() for line in text.splitlines() if "preview_frame_hz=" in line]
    return lines[-1] if lines else text.strip()


def run(args: argparse.Namespace) -> int:
    preview = preview_payload(args.preview_width, args.preview_height, 0, args.rgb332)
    with socket.create_connection((args.host, args.port), timeout=2.0) as client:
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client.sendall(base_frame())
        client.sendall(filmstrip_asset())
        started = time.perf_counter()
        deadline = started + args.seconds
        sequence = 3
        frame = 0
        while time.perf_counter() < deadline:
            target = started + frame / args.fps
            remaining = target - time.perf_counter()
            if remaining > 0:
                time.sleep(remaining)
            if frame % 4 == 0:
                preview = preview_payload(args.preview_width, args.preview_height, frame, args.rgb332)
            client.sendall(
                header(
                    b"VJVP",
                    sequence,
                    args.preview_width,
                    args.preview_height,
                    args.preview_x,
                    args.preview_y,
                    len(preview), version=2 if args.rgb332 else 1,
                )
                + preview
            )
            sequence += 1
            scroll = (frame * args.scroll_px_per_frame) % 800
            client.sendall(header(b"VJFT", sequence, int(scroll), 1600, 120, 360, 0, float(scroll), flags=3))
            sequence += 1
            frame += 1
        stats_line = read_stats(args.host, args.telnet_port)
    print(stats_line)
    stats = {key: float(value) for key, value in STATS_RE.findall(stats_line)}
    preview_hz = stats.get("preview_frame_hz", 0.0)
    present_hz = stats.get("framebuffer_present_hz", 0.0)
    tick_hz = stats.get("tick_hz", 0.0)
    passed = preview_hz >= args.minimum_fps and present_hz >= args.minimum_fps and tick_hz >= args.minimum_fps
    print(
        f"{'PASS' if passed else 'FAIL'} minimum={args.minimum_fps:.1f} "
        f"preview={preview_hz:.1f} present={present_hz:.1f} tick={tick_hz:.1f} sent={frame}"
    )
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="169.254.168.58")
    parser.add_argument("--port", type=int, default=50005)
    parser.add_argument("--telnet-port", type=int, default=2323)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--minimum-fps", type=float, default=30.0)
    parser.add_argument("--preview-width", type=int, default=160)
    parser.add_argument("--preview-height", type=int, default=90)
    parser.add_argument("--preview-x", type=int, default=80)
    parser.add_argument("--preview-y", type=int, default=0)
    parser.add_argument("--scroll-px-per-frame", type=float, default=0.75)
    parser.add_argument("--rgb332", action="store_true")
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
