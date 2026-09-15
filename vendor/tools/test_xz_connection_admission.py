"""Exercise the real receiver listener with loopback sockets, without DirectFB.

Run with Python on Linux (or WSL); requires gcc.
"""
import pathlib
import socket
import struct
import subprocess
import tempfile
import time

SOURCE = pathlib.Path(__file__).parent / "xz_runtime/xz_directfb_hook.c"


def function(source, name):
    start = source.index("static ", source.rfind("\n", 0, source.index(name + "(")))
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = SOURCE.read_text()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    declarations = """
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
static int vjfs_client_fd = -1;
static int latest_visible, latest_exclusive, preview_valid, tick_valid;
static uint32_t last_activity_ms;
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static void log_line(const char *s) { (void)s; }
struct __attribute__((packed)) vjfs_header {
 char magic[4]; uint8_t version, deck; uint16_t flags; uint32_t sequence;
 uint16_t width, height, x, y; uint32_t payload_length; float playhead;
};
static void handle_vjfs_client(int client) {
 char data[4096];
 if (recv(client, data, 28, MSG_WAITALL) != 28) return;
 if (memcmp(data, "VJFS", 4) != 0) return;
 send(client, "R", 1, 0);
 while (recv(client, data, sizeof(data), 0) > 0) {}
}
"""
    names = ["vjfs_client_thread", "vjfs_listener"]
    if "static int vjfs_client_has_header(" in source:
        names.insert(0, "vjfs_client_has_header")
    program = declarations + f"\n#define VJFS_PORT {port}\n"
    program += "\n".join(function(source, name) for name in names)
    program += "\nint main(void) { vjfs_listener(NULL); return 0; }\n"
    with tempfile.TemporaryDirectory(prefix="xz-admission-") as tmp:
        path = pathlib.Path(tmp)
        (path / "test.c").write_text(program)
        subprocess.run(["gcc", "-pthread", "-Wall", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True)
        process = subprocess.Popen([str(path / "test")])
        try:
            time.sleep(0.15)
            with socket.create_connection(("127.0.0.1", port)) as stream:
                header = struct.pack("<4sBBHIHHHHIf", b"VJFS", 1, 1, 1, 1, 1, 1, 0, 0, 2, 0)
                stream.sendall(header + b"\xff\x07")
                stream.settimeout(2)
                assert stream.recv(1) == b"R", "first header must reach the receiver intact"
                for _ in range(10):
                    with socket.create_connection(("127.0.0.1", port)):
                        pass
                stream.settimeout(0.7)
                try:
                    result = stream.recv(1)
                    raise AssertionError(f"probe evicted the active screen stream: {result!r}")
                except socket.timeout:
                    print("PASS: 10 empty discovery probes preserve the active screen stream")
                with socket.create_connection(("127.0.0.1", port)) as silent:
                    silent.settimeout(2)
                    assert silent.recv(1) == b"", "silent admission must time out"
                with socket.create_connection(("127.0.0.1", port)) as invalid:
                    invalid.sendall(b"INVALID" + bytes(21))
                    invalid.settimeout(2)
                    try:
                        assert invalid.recv(1) == b""
                    except ConnectionResetError:
                        pass
                try:
                    result = stream.recv(1)
                    raise AssertionError(f"silent or invalid probe evicted stream: {result!r}")
                except socket.timeout:
                    print("PASS: silent and invalid probes preserve the active screen stream")
                with socket.create_connection(("127.0.0.1", port)) as replacement:
                    replacement.sendall(header[:4])
                    time.sleep(0.02)
                    replacement.sendall(header[4:] + b"\x00\x00")
                    replacement.settimeout(2)
                    assert replacement.recv(1) == b"R", "fragmented header must reach receiver intact"
                    assert stream.recv(1) == b"", "real replacement must take over"
                    print("PASS: a replacement screen stream can still take over")
        finally:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    main()
