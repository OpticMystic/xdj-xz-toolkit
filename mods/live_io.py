"""Bounded transfers through the private test helper, after its ABI check."""
import hashlib
import shlex
import socket
import time
from device_smoke import command
from tools.xz_nand_dump import open_telnet

HELPER = "/dev/shm/xz-dev-0f95f510db9a/transfer"
HOST = "169.254.168.59"
CLIENT = "169.254.168.58"


def transfer(mode, remote, count, data=None):
    if mode not in ("send", "receive") or not 0 < count <= 64 * 1024 * 1024:
        raise ValueError("Invalid bounded transfer")
    redirection = "<" if mode == "send" else ">"
    with open_telnet(HOST, source_address=(CLIENT, 0)) as shell:
        # noclobber protects existing files for receive operations.
        script = f"set -C; {HELPER} {mode} {count} {HOST} {CLIENT} {redirection} {shlex.quote(remote)}; exit\r\n"
        shell.sendall(script.encode("ascii"))
        deadline = time.monotonic() + 5
        while True:
            try:
                connection = socket.create_connection((HOST, 50008), timeout=0.5, source_address=(CLIENT, 0))
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        with connection:
            connection.settimeout(20)
            if mode == "receive":
                if data is None or len(data) != count:
                    raise ValueError("Transfer data length mismatch")
                connection.sendall(data)
                connection.shutdown(socket.SHUT_WR)
                while connection.recv(1024):
                    pass
                result = None
            else:
                result = bytearray()
                while len(result) < count:
                    part = connection.recv(min(262144, count - len(result)))
                    if not part:
                        raise ValueError("Short device transfer")
                    result.extend(part)
    if mode == "receive":
        digest = command(HOST, f"md5sum {shlex.quote(remote)}").split()[0]
        if digest != hashlib.md5(data).hexdigest():
            raise ValueError("Device file checksum mismatch")
    return bytes(result) if result is not None else None
