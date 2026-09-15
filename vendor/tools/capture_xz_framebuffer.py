#!/usr/bin/env python3
"""Capture the volatile XDJ-XZ RGB565 framebuffer through its telnet shell."""

from __future__ import annotations

import argparse
import pathlib
import shutil
import socket
import subprocess

from tools.xz_nand_dump import open_telnet


PAGE_BYTES = 800 * 480 * 2


def capture(host: str, pc_host: str, output: pathlib.Path, port: int, device: str = "/dev/fb1") -> pathlib.Path:
    pages = 2 if device == "/dev/fb1" else 1
    frame_bytes = PAGE_BYTES * pages
    output.parent.mkdir(parents=True, exist_ok=True)
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((pc_host, port))
    server.listen(1)
    server.settimeout(12)
    telnet = open_telnet(host)
    telnet.sendall(
        f"dd if={device} bs={frame_bytes} count=1 2>/dev/null | nc {pc_host} {port}; exit\r\n".encode("ascii")
    )
    connection, _ = server.accept()
    connection.settimeout(8)
    data = bytearray()
    while len(data) < frame_bytes:
        chunk = connection.recv(min(262_144, frame_bytes - len(data)))
        if not chunk:
            break
        data.extend(chunk)
    connection.close()
    telnet.close()
    server.close()
    if len(data) != frame_bytes:
        raise RuntimeError(f"Framebuffer capture was {len(data)} bytes; expected {frame_bytes}")
    raw = output.with_suffix(".rgb565")
    raw.write_bytes(data)
    full = output.with_name(f"{output.stem}-full.png")
    page0 = output.with_name(f"{output.stem}-page0.png")
    page1 = output.with_name(f"{output.stem}-page1.png")
    full_height = 480 * pages
    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pixel_format", "rgb565le",
        "-video_size", f"800x{full_height}", "-i", str(raw), "-frames:v", "1", str(full),
    ], check=True)
    if pages == 1:
        shutil.copyfile(full, output)
        print(f"CAPTURED bytes={len(data)} device={device} output={output}")
        return output
    for page, y in ((page0, 0), (page1, 480)):
        subprocess.run([
            "ffmpeg", "-y", "-loglevel", "error", "-i", str(full),
            "-vf", f"crop=800:480:0:{y}", "-frames:v", "1", str(page),
        ], check=True)
    active = max((page0, page1), key=lambda path: path.stat().st_size)
    shutil.copyfile(active, output)
    print(f"CAPTURED bytes={len(data)} active={active.name} output={output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--host", default="169.254.168.58")
    parser.add_argument("--pc-host", default="169.254.123.182")
    parser.add_argument("--port", type=int, default=4246)
    parser.add_argument("--device", default="/dev/fb1", choices=["/dev/fb0", "/dev/fb1"])
    args = parser.parse_args()
    capture(args.host, args.pc_host, args.output, args.port, args.device)


if __name__ == "__main__":
    main()
