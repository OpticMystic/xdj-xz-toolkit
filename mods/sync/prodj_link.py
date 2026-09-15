"""Receive-only Pro DJ Link beat decoding; no packets are transmitted."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Beat:
    device: int
    name: str
    bpm: float
    beat_in_bar: int


def decode_beat(packet: bytes) -> Beat | None:
    if len(packet) != 0x60 or packet[:10] != b"Qspt1WmJOL":
        return None
    if packet[0x0a] != 0x28 or packet[0x1f:0x21] != b"\x01\x00":
        return None
    if int.from_bytes(packet[0x22:0x24], "big") != 0x3c:
        return None
    device = packet[0x21]
    if device not in (1, 2, 3, 4, 17, 18) or packet[0x5f] != device:
        return None
    beat = packet[0x5c]
    pitch = int.from_bytes(packet[0x55:0x58], "big")
    raw_bpm = int.from_bytes(packet[0x5a:0x5c], "big")
    if beat not in (1, 2, 3, 4) or not 0 < pitch <= 0x200000:
        return None
    if raw_bpm in (0, 0xffff):
        return None
    bpm = raw_bpm * pitch / (100 * 0x100000)
    if not 20 <= bpm <= 999:
        return None
    name = packet[0x0b:0x1f].split(b"\0", 1)[0].decode("ascii", errors="replace")
    return Beat(device, name, bpm, beat)
