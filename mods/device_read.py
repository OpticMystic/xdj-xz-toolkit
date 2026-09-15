"""Read firmware evidence through the existing XZ diagnostic shell. No writes on XZ."""

from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import re
import socket
import sys
import time
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "vendor"))
from tools.xz_nand_dump import open_telnet


def read_file(host: str, remote: str, *, limit: int = 16 * 1024 * 1024) -> bytes:
    if not re.fullmatch(r"/(?:proc/[0-9]+/exe|root/pdj/(?:rbp|app\.rev)|lib/[a-zA-Z0-9_.-]+)", remote):
        raise ValueError("Only the application executable/revision and system libraries may be read")
    token = uuid.uuid4().hex
    begin, end = f"XZ_BEGIN_{token}", f"XZ_END_{token}"
    response = bytearray()
    with open_telnet(host) as shell:
        shell.sendall(f"echo {begin}; base64 {remote}; echo {end}; exit\r\n".encode("ascii"))
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                chunk = shell.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            response.extend(chunk)
            if len(response) > limit * 2:
                raise ValueError("Device response exceeded the evidence size limit")
            # The shell echoes the command before running it. Only a complete
            # standalone marker line indicates completion, never that echo.
            if (b"\n" + end.encode() + b"\r") in response:
                break
    lines = bytes(response).replace(b"\r", b"").split(b"\n")
    first, last = lines.index(begin.encode()), lines.index(end.encode())
    result = base64.b64decode(b"".join(lines[first + 1:last]), validate=True)
    if not result or len(result) > limit:
        raise ValueError("Empty or oversized device evidence")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("remote")
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--host", default="169.254.168.59")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output already exists; choose a new evidence filename")
    data = read_file(args.host, args.remote)
    with args.output.open("xb") as output:
        output.write(data)
    print(f"{len(data)} bytes; SHA256 {hashlib.sha256(data).hexdigest()}; {args.output}")


if __name__ == "__main__":
    main()
