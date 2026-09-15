import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time
import unittest

from prodj_link import decode_beat
from xz_to_link import LinkClient, SelectedClock, bind_beats


ROOT = Path(__file__).resolve().parents[4]
FIXTURES = json.loads(Path(__file__).with_name("fixtures.json").read_text())


def packet(name="xz_120_downbeat"):
    return bytes.fromhex(FIXTURES[name]["hex"])


class BeatTests(unittest.TestCase):
    def test_documented_fields_and_pitch_scaling(self):
        for fixture in FIXTURES.values():
            beat = decode_beat(bytes.fromhex(fixture["hex"]))
            self.assertIsNotNone(beat)
            self.assertAlmostEqual(beat.bpm, fixture["bpm"])
            self.assertEqual(beat.device, fixture["device"])
            self.assertEqual(beat.beat_in_bar, fixture["beat"])

    def test_malformed_and_other_packet_types(self):
        valid = packet()
        for size in range(96):
            self.assertIsNone(decode_beat(valid[:size]))
        self.assertIsNone(decode_beat(valid + b"\0"))
        for offset, value in [(0, 0), (10, 3), (31, 0), (32, 2), (33, 33),
                              (34, 1), (35, 59), (92, 0), (92, 5), (95, 2)]:
            broken = bytearray(valid)
            broken[offset] = value
            self.assertIsNone(decode_beat(bytes(broken)), (offset, value))
        for pitch, bpm in [(0, 12000), (0x200001, 12000), (0x100000, 0xffff),
                           (0x100000, 0), (0x100000, 100), (0x200000, 60000)]:
            broken = bytearray(valid)
            broken[0x55:0x58] = pitch.to_bytes(3, "big")
            broken[0x5a:0x5c] = bpm.to_bytes(2, "big")
            self.assertIsNone(decode_beat(bytes(broken)))

    def test_selected_ip_and_player_duplicates_stale_resume(self):
        clock = SelectedClock("192.168.1.10", 1, 3.5)
        self.assertIsNone(clock.accept(packet(), "192.168.1.11", 1))
        self.assertIsNone(clock.accept(packet("xz_deck2"), clock.source_ip, 1))
        self.assertFalse(clock.stale(20))
        self.assertIsNotNone(clock.accept(packet(), clock.source_ip, 21))
        self.assertIsNone(clock.accept(packet(), clock.source_ip, 21.01))
        self.assertIsNotNone(clock.accept(packet(), clock.source_ip, 23))
        self.assertFalse(clock.stale(26.49))
        self.assertTrue(clock.stale(26.5))
        self.assertFalse(clock.stale(40))
        self.assertIsNotNone(clock.accept(packet(), clock.source_ip, 41))

    def test_occupied_udp_port_not_shared(self):
        with bind_beats("127.0.0.1", 0) as first:
            with self.assertRaises(OSError):
                bind_beats("127.0.0.1", first.getsockname()[1])

    def test_daemon_rejection_is_visible(self):
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            server.listen()
            def serve():
                with server.accept()[0] as connection:
                    connection.recv(4096)
                    connection.sendall(b'unsupported "capabilities"\n')
            thread = threading.Thread(target=serve)
            thread.start()
            with self.assertRaisesRegex(RuntimeError, "unsupported"):
                LinkClient(server.getsockname()[1])
            thread.join(2)
            self.assertFalse(thread.is_alive())


@unittest.skipUnless(os.environ.get("XZ_TEST_LINK_DAEMON") == "1", "Set XZ_TEST_LINK_DAEMON=1 for native daemon integration")
class NativeDaemonTests(unittest.TestCase):
    def test_real_addon_tempo_phase_and_stale_silence(self):
        import re
        daemon = ROOT / "packages/link-daemon/daemon.cjs"
        process = subprocess.Popen([os.environ.get("XZ_NODE", "node"), str(daemon), "--port", "0"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        link = None
        try:
            started = process.stdout.readline().strip()
            self.assertRegex(started, r"^listening on \d+$")
            link = LinkClient(int(started.split()[-1]))
            # Prevent this test daemon advertising changes to real Link peers.
            link.command("enable 0", "status ")
            beat = decode_beat(packet("xz_126_beat3"))
            link.follow(beat, time.monotonic())
            status = link.command("status", "status ")
            self.assertAlmostEqual(float(re.search(r":bpm ([\d.]+)", status)[1]), 126, places=3)
            phase = float(re.search(r":phase ([\d.]+)", status)[1])
            self.assertLess(abs(phase - 2), 0.1, status)
            clock = SelectedClock("192.168.1.10", 1, 0.25)
            clock.accept(packet(), clock.source_ip, 1)
            self.assertTrue(clock.stale(1.3))
            status = link.command("status", "status ")
            self.assertIn(":bpm 126", status)
        finally:
            if link:
                link.close()
            process.terminate()
            process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
