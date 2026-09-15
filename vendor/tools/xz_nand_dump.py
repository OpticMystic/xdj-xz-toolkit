"""Read-only XDJ-XZ NAND partition capture over the diagnostic telnet shell."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import socket
import time


IAC = 255
DO = 253
WONT = 252
WILL = 251


def open_telnet(
    host: str,
    port: int = 2323,
    source_address: tuple[str, int] | None = None,
) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=5, source_address=source_address)
    sock.settimeout(0.4)
    try:
        greeting = sock.recv(4096)
    except socket.timeout:
        greeting = b""

    response = bytearray()
    index = 0
    while index + 2 < len(greeting):
        if greeting[index] == IAC and greeting[index + 1] in (DO, WILL):
            response += bytes((IAC, WONT if greeting[index + 1] == DO else DO, greeting[index + 2]))
            index += 3
        else:
            index += 1
    if response:
        sock.sendall(response)
    return sock


def run_text_command(
    host: str,
    command: str,
    timeout: float = 8,
    source_address: tuple[str, int] | None = None,
) -> str:
    sock = open_telnet(host, source_address=source_address)
    marker = "__XZ_COMMAND_DONE__"
    sock.sendall(f"cd /; {command}; echo {marker}; exit\r\n".encode("ascii"))
    output = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            chunk = sock.recv(32768)
        except socket.timeout:
            if marker.encode() in output:
                break
            continue
        if not chunk:
            break
        output += chunk
    sock.close()
    text = output.decode("utf-8", "replace").replace("\r", "")
    marker_index = text.rfind(marker)
    return text[:marker_index] if marker_index >= 0 else text


def parse_partitions(proc_mtd: str) -> list[dict[str, object]]:
    partitions = []
    for line in proc_mtd.splitlines():
        match = re.match(r'mtd(\d+):\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+"([^"]+)"', line)
        if not match:
            continue
        index = int(match.group(1))
        if index > 13:
            continue  # mtd14-mtd16 are overlapping aggregate views.
        partitions.append({"index": index, "size": int(match.group(2), 16), "name": match.group(3)})
    return partitions


def dump_partitions(host: str, pc_host: str, output_dir: pathlib.Path, port: int = 4245) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    metadata = run_text_command(
        host,
        "echo PROC_MTD; cat /proc/mtd; echo UBINFO; ubinfo -a; "
        "echo NAND_GEOMETRY; cat /sys/class/mtd/mtd0/writesize; cat /sys/class/mtd/mtd0/oobsize",
        timeout=12,
    )
    (output_dir / "device-metadata.txt").write_text(metadata, encoding="utf-8")
    partitions = parse_partitions(metadata)
    if len(partitions) != 14:
        raise RuntimeError(f"Expected mtd0-mtd13, found {len(partitions)} partition records")

    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((pc_host, port))
    server.listen(1)
    artifacts = []

    try:
        for partition in partitions:
            index = int(partition["index"])
            expected_size = int(partition["size"])
            safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", str(partition["name"]))
            final_path = output_dir / f"mtd{index:02d}-{safe_name}.bin"
            partial_path = final_path.with_suffix(".bin.partial")

            print(f"START mtd{index} {partition['name']} expected={expected_size}", flush=True)
            server.settimeout(15)
            telnet = open_telnet(host)
            command = (
                # This 2014-era mtd-utils build cannot query the kernel's
                # mtdNro aliases. nanddump itself opens /dev/mtdN O_RDONLY.
                f"cd /; nanddump -q --bb=padbad --omitoob /dev/mtd{index} "
                f"| nc {pc_host} {port}; exit\r\n"
            )
            telnet.sendall(command.encode("ascii"))
            connection, _ = server.accept()
            connection.settimeout(30)
            digest = hashlib.sha256()
            received = 0
            with partial_path.open("wb") as output:
                while True:
                    chunk = connection.recv(262144)
                    if not chunk:
                        break
                    output.write(chunk)
                    digest.update(chunk)
                    received += len(chunk)
            connection.close()
            telnet.close()

            if received != expected_size:
                raise RuntimeError(
                    f"mtd{index} size mismatch: received {received}, expected {expected_size}; "
                    f"partial retained at {partial_path}"
                )
            partial_path.replace(final_path)
            artifact = {
                **partition,
                "file": final_path.name,
                "bytes": received,
                "sha256": digest.hexdigest(),
                "capture": "nanddump corrected data, OOB omitted, bad blocks padded",
            }
            artifacts.append(artifact)
            print(f"PASS  mtd{index} bytes={received} sha256={digest.hexdigest()}", flush=True)
    finally:
        server.close()

    manifest = {
        "source": f"XDJ-XZ {host}",
        "pc_receiver": pc_host,
        "capture_kind": "read-only non-overlapping NAND partitions mtd0-mtd13",
        "aggregate_partitions_omitted": ["mtd14 block_boot", "mtd15 block_linux", "mtd16 block_all"],
        "artifacts": artifacts,
    }
    (output_dir / "nand-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"COMPLETE {output_dir.resolve()}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=pathlib.Path)
    parser.add_argument("--host", default="169.254.168.58")
    parser.add_argument("--pc-host", default="169.254.168.59")
    parser.add_argument("--port", type=int, default=4245)
    args = parser.parse_args()
    dump_partitions(args.host, args.pc_host, args.output_dir, args.port)


if __name__ == "__main__":
    main()
