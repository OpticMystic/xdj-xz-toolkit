"""Run isolated ARM tests in a fresh RAM directory; never restart or hook rbp."""
from __future__ import annotations

import argparse
import base64
import hashlib
import gzip
import json
import pathlib
import socket
import sys
import time
import uuid
import struct
import subprocess
import wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "vendor"))
from tools.xz_nand_dump import open_telnet
from tools.deploy_xz_hook_live import command as terminal_command

FILES = ("libxz-mods-development.so", "libxz-directfb-mods-test.so", "runtime-smoke", "cue-test", "stem-test", "decode-test", "native-decode-test", "settings-test")


def command(host: str, text: str, timeout: float = 15) -> str:
    token = uuid.uuid4().hex
    start, end = "BEGIN_" + token, "END_" + token
    data = bytearray()
    invocation = f"echo {start}; {text}; echo {end}; exit\r\n".encode("ascii")
    if len(invocation) > 900:
        raise ValueError("Diagnostic command exceeds the old shell's line budget")
    source = ("169.254.168.58", 0) if host == "169.254.168.59" else None
    with open_telnet(host, source_address=source) as shell:
        shell.sendall(invocation)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                part = shell.recv(65536)
            except socket.timeout:
                continue
            if not part:
                break
            data.extend(part)
            if len(data) > 1024 * 1024:
                raise RuntimeError("Diagnostic output exceeded limit")
            if b"\n" + end.encode() + b"\r" in data:
                break
    lines = bytes(data).replace(b"\r", b"").decode("ascii", "replace").splitlines()
    return "\n".join(lines[lines.index(start) + 1:lines.index(end)])


def upload(host: str, remote: str, data: bytes) -> None:
    encoded = base64.b64encode(gzip.compress(data, mtime=0)).decode("ascii")
    with open_telnet(host) as shell:
        terminal_command(shell, f"umask 077; : > {remote}.b64")
        for offset in range(0, len(encoded), 512):
            terminal_command(shell, f"printf '%s' '{encoded[offset:offset + 512]}' >> {remote}.b64", 0.015)
        terminal_command(shell, f"base64 -d {remote}.b64 | gzip -dc > {remote}", 0.25)
    digest = command(host, f"md5sum {remote}").strip().split()[0]
    if digest != hashlib.md5(data).hexdigest():
        raise RuntimeError(f"RAM transfer verification failed: {remote}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--host", default="169.254.168.59")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--fast-transfer", action="store_true", help="Use the existing private RAM transfer helper")
    args = parser.parse_args()
    inputs = {name: (args.build / name).read_bytes() for name in FILES}
    if any(not data.startswith(b"\x7fELF") or len(data) > 2 * 1024 * 1024 for data in inputs.values()):
        raise ValueError("Expected bounded ELF build artifacts")
    fixture = args.build / "decode-fixture.wav"
    with wave.open(str(fixture), "wb") as output:
        output.setnchannels(2); output.setsampwidth(2); output.setframerate(44100)
        output.writeframes(b"".join(struct.pack("<h", i % 60001 - 30000) for i in range(88200)))
    compressed = args.build / "decode-fixture.flac"
    subprocess.run([args.ffmpeg, "-y", "-v", "error", "-i", str(fixture), "-c:a", "flac", str(compressed)], check=True)
    inputs[fixture.name] = fixture.read_bytes()
    inputs[compressed.name] = compressed.read_bytes()
    before = command(args.host, "pidof rbp").strip()
    if not before:
        raise RuntimeError("No running stock app; cannot check continuity")
    remote = "/dev/shm/xz-mods-test-" + uuid.uuid4().hex
    status = command(args.host, f"mkdir {remote}; echo mkdir_status=$?")
    if "mkdir_status=0" not in status:
        raise RuntimeError("Could not create isolated RAM directory")
    evidence = {"rbp_before": before, "remote": remote, "tests": {}, "writes": "new RAM directory only",
                "artifact_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in inputs.items()}}
    try:
        for name, data in inputs.items():
            if args.fast_transfer:
                from live_io import HOST, transfer
                if args.host != HOST:
                    raise ValueError("Private transfer helper is bound to the verified device address")
                transfer("receive", remote + "/" + name, len(data), data)
            else:
                upload(args.host, remote + "/" + name, data)
        command(args.host, f"chmod 700 {remote}/runtime-smoke {remote}/cue-test {remote}/stem-test {remote}/decode-test")
        command(args.host, f"chmod 700 {remote}/native-decode-test")
        command(args.host, f"chmod 700 {remote}/settings-test")
        tests = {"runtime-smoke": "sh -c 'ulimit -c 0; i=0; while [ $i -lt 10 ]; do "
                 + f"{remote}/runtime-smoke {remote}/libxz-mods-development.so {remote}/libxz-directfb-mods-test.so"
                 + " || exit $?; i=$((i+1)); done'",
                 "cue-test": remote + "/cue-test", "stem-test": remote + "/stem-test",
                 "native-decode-test": remote + "/native-decode-test",
                 "settings-test": remote + "/settings-test",
                 "decode-test": f"{remote}/decode-test {remote}/decode-fixture.flac {remote}/decode-fixture.wav"}
        failures = []
        for name, invocation in tests.items():
            result = command(args.host, invocation + "; echo test_status=$?")
            evidence["tests"][name] = result
            if "test_status=0" not in result:
                failures.append(name)
        evidence["rbp_after"] = command(args.host, "pidof rbp").strip()
        if evidence["rbp_after"] != before:
            raise RuntimeError("Application PID changed during isolated tests")
        if failures:
            evidence["result"] = "FAIL: " + ", ".join(failures)
            raise RuntimeError(json.dumps(evidence))
        evidence["result"] = "PASS isolated ARM tests; native feature hooks not activated"
    finally:
        # Fixed filenames in the freshly generated directory, no recursive delete.
        (args.build / "device-smoke.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf8")
        tag = evidence["artifact_sha256"]["libxz-mods-development.so"][:12]
        (args.build / f"device-smoke-{tag}.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf8")
        for name in inputs:
            command(args.host, f"rm -f {remote}/{name} {remote}/{name}.b64")
        command(args.host, f"rmdir {remote}")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
