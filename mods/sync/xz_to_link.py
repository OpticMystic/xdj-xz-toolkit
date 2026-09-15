"""Forward one explicitly selected XZ player clock into the local Link daemon."""

import argparse
import ipaddress
import json
import math
import selectors
import socket
import sys
import time

from prodj_link import decode_beat


class LinkClient:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=0.25)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = b""
        try:
            reply = self.command("capabilities", "capabilities ")
            if ":align-phase true" not in reply:
                raise RuntimeError("Link daemon does not support align-phase")
        except BaseException:
            self.close()
            raise

    def close(self):
        self.socket.close()

    def command(self, command, prefix):
        self.socket.sendall((command + "\n").encode("ascii"))
        deadline = time.monotonic() + 0.25
        for _ in range(100):
            while b"\n" not in self.buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Link daemon response deadline exceeded")
                self.socket.settimeout(remaining)
                chunk = self.socket.recv(4096)
                if not chunk:
                    raise ConnectionError("Link daemon disconnected")
                self.buffer += chunk
                if len(self.buffer) > 16384:
                    raise RuntimeError("Link daemon response exceeded size limit")
            line, self.buffer = self.buffer.split(b"\n", 1)
            line = line.decode("ascii")
            if line.startswith(("error ", "unsupported ")):
                raise RuntimeError(line)
            if line.startswith(prefix):
                return line
        raise RuntimeError("Link daemon response limit exceeded")

    def follow(self, beat, received_at, latency_ms=0):
        self.command(f"bpm {beat.bpm:.6f}", "status ")
        age = time.monotonic() - received_at
        if age > 0.1:
            raise TimeoutError("Beat became stale while updating Link tempo")
        phase = (beat.beat_in_bar - 1 + (age + latency_ms / 1000) * beat.bpm / 60) % 4
        result = self.command(f"align-phase {phase:.9f} 4 1", "align-phase ")
        if ":applied true" not in result:
            raise RuntimeError("Link daemon did not apply phase alignment")


class SelectedClock:
    def __init__(self, source_ip, device, stale_seconds):
        self.source_ip = source_ip
        self.device = device
        self.stale_seconds = stale_seconds
        self.last_received = None
        self.last_packet = None
        self.active = False

    def accept(self, packet, source_ip, now):
        if source_ip != self.source_ip:
            return None
        beat = decode_beat(packet)
        if beat is None or beat.device != self.device:
            return None
        if self.last_received is not None:
            elapsed = now - self.last_received
            # Beat packets have no sequence counter. Suppress duplicate bursts;
            # identical payloads a bar later are valid and must still pass.
            if elapsed < 0.025 or (packet == self.last_packet and elapsed < 0.1):
                return None
        self.last_received = now
        self.last_packet = packet
        self.active = True
        return beat

    def stale(self, now):
        if self.active and now - self.last_received >= self.stale_seconds:
            self.active = False
            return True
        return False


def bind_beats(address, port=50001):
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        listener.bind((address, port))
        listener.setblocking(False)
        return listener
    except BaseException:
        listener.close()
        raise


def emit(state, **fields):
    print(json.dumps({"state": state, **fields}), flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ip", required=True, type=ipaddress.IPv4Address)
    parser.add_argument("--device-number", required=True, type=int, choices=(1, 2, 3, 4, 17, 18))
    parser.add_argument("--bind", default="0.0.0.0", type=ipaddress.IPv4Address)
    parser.add_argument("--daemon-port", type=int, default=17000)
    parser.add_argument("--stale-seconds", type=float, default=3.5)
    parser.add_argument("--latency-ms", type=float, default=0,
                        help="Measured signed phase compensation; positive advances Link")
    parser.add_argument("--observe", action="store_true", help="Decode beats without connecting to or changing Link")
    args = parser.parse_args(argv)
    if not 1 <= args.daemon_port <= 65535:
        parser.error("daemon-port must be 1..65535")
    if not math.isfinite(args.stale_seconds) or not 0.25 <= args.stale_seconds <= 10:
        parser.error("stale-seconds must be 0.25..10")
    if not math.isfinite(args.latency_ms) or abs(args.latency_ms) > 500:
        parser.error("latency-ms must be -500..500")
    clock = SelectedClock(str(args.source_ip), args.device_number, args.stale_seconds)
    link = None
    try:
        with bind_beats(str(args.bind)) as listener, selectors.DefaultSelector() as selector:
            selector.register(listener, selectors.EVENT_READ)
            if not args.observe:
                link = LinkClient(args.daemon_port)
            emit("waiting", source=str(args.source_ip), device=args.device_number, observe=args.observe)
            while True:
                if not selector.select(0.1):
                    if clock.stale(time.monotonic()):
                        emit("stale", detail="Clock forwarding stopped; Link transport unchanged")
                    continue
                # Drain the ready socket before making any TCP calls, so an old
                # queued beat cannot pull the session back to an earlier phase.
                latest = None
                batch_started = time.monotonic()
                for _ in range(256):
                    try:
                        packet, address = listener.recvfrom(2048)
                    except BlockingIOError:
                        break
                    beat = decode_beat(packet)
                    if address[0] == clock.source_ip and beat and beat.device == clock.device:
                        latest = packet, address[0], time.monotonic()
                else:
                    raise RuntimeError("Beat socket flood; stopped without forwarding queued data")
                if clock.stale(time.monotonic()):
                    emit("stale", detail="Clock forwarding stopped; Link transport unchanged")
                if latest is not None:
                    if time.monotonic() - batch_started > 0.1:
                        raise RuntimeError("Beat receive backlog exceeded timing budget")
                    packet, source, received = latest
                    beat = clock.accept(packet, source, received)
                    if beat is None:
                        continue
                    if link:
                        link.follow(beat, received, args.latency_ms)
                    emit("observed" if args.observe else "following", bpm=beat.bpm,
                         beat=beat.beat_in_bar, device=beat.device, name=beat.name)
    except KeyboardInterrupt:
        emit("stopped")
        return 0
    except (OSError, RuntimeError, UnicodeError) as error:
        emit("error", detail=str(error))
        return 1
    finally:
        if link:
            link.close()


if __name__ == "__main__":
    sys.exit(main())
