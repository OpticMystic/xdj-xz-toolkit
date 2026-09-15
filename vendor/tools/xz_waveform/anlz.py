"""Size-preserving PWV6/PWV7 to PWV4/PWV5 waveform adaptation."""

from __future__ import annotations

import dataclasses
import struct


@dataclasses.dataclass(frozen=True)
class Section:
    tag: bytes
    offset: int
    header_length: int
    total_length: int


class AnlzFile:
    def __init__(self, data: bytes):
        if len(data) < 12 or data[:4] != b"PMAI":
            raise ValueError("Not a Rekordbox PMAI analysis file")
        header_length, declared_length = struct.unpack_from(">II", data, 4)
        if declared_length != len(data):
            raise ValueError(f"PMAI length guard mismatch ({declared_length} != {len(data)})")
        if not 12 <= header_length <= len(data):
            raise ValueError("Invalid PMAI header length")
        self._data = bytearray(data)
        self._sections = self._parse_sections(header_length)

    def _parse_sections(self, offset: int) -> tuple[Section, ...]:
        sections = []
        while offset < len(self._data):
            if offset + 12 > len(self._data):
                raise ValueError("Truncated ANLZ section header")
            tag = bytes(self._data[offset:offset + 4])
            header_length, total_length = struct.unpack_from(">II", self._data, offset + 4)
            if not 12 <= header_length <= total_length or offset + total_length > len(self._data):
                raise ValueError(f"Invalid {tag!r} section lengths at {offset:#x}")
            sections.append(Section(tag, offset, header_length, total_length))
            offset += total_length
        return tuple(sections)

    @property
    def sections(self) -> tuple[Section, ...]:
        return self._sections

    def section(self, tag: bytes) -> Section | None:
        return next((section for section in self._sections if section.tag == tag), None)

    def wave_entries(self, tag: bytes) -> tuple[Section, int, int, int, memoryview]:
        section = self.section(tag)
        if section is None:
            raise KeyError(tag.decode("ascii", "replace"))
        body = section.offset + 12
        entry_size, count = struct.unpack_from(">II", self._data, body)
        if tag in (b"PWV4", b"PWV5"):
            entries_offset = body + 12
        elif tag in (b"PWV6", b"PWV7"):
            entries_offset = body + 8
        else:
            raise ValueError(f"Unsupported waveform tag {tag!r}")
        entries_size = entry_size * count
        if entries_offset + entries_size > section.offset + section.total_length:
            raise ValueError(f"Truncated {tag.decode()} entries")
        return (
            section,
            entry_size,
            count,
            entries_offset,
            memoryview(self._data)[entries_offset:entries_offset + entries_size],
        )

    def to_bytes(self) -> bytes:
        return bytes(self._data)


def _band_rgb(mid: int, high: int, low: int) -> tuple[int, int, int, int]:
    """Map Rekordbox mid/high/low energy to blue/orange/white RGB energy."""
    red = max(mid, high)
    green = max((mid * 115) // 255, high)  # orange mids, white highs
    blue = max(low, high)
    height = max(mid, high, low)
    return red, green, blue, height


def _sample_three_band(entries: memoryview, count: int, index: int, target_count: int) -> tuple[int, int, int]:
    if count <= 0:
        return 0, 0, 0
    source_index = min(count - 1, (index * count) // max(1, target_count))
    offset = source_index * 3
    return entries[offset], entries[offset + 1], entries[offset + 2]


def adapt_three_band_to_rgb(ext_data: bytes, two_ex_data: bytes) -> tuple[bytes, dict[str, object]]:
    """Adapt true 3-band atoms into the existing fixed-size RGB atoms.

    The EXT file length and section table remain unchanged. PWV4 keeps its
    original geometry channels and receives band-derived RGB channels. PWV5
    keeps the existing renderer contract and receives band-derived RGB plus
    height. PWV7 is preferred for detail; PWV6 is resampled as a fallback.
    """

    ext = AnlzFile(ext_data)
    two_ex = AnlzFile(two_ex_data)
    _, pwv4_size, pwv4_count, _, pwv4 = ext.wave_entries(b"PWV4")
    _, pwv5_size, pwv5_count, _, pwv5 = ext.wave_entries(b"PWV5")
    _, pwv6_size, pwv6_count, _, pwv6 = two_ex.wave_entries(b"PWV6")
    if (pwv4_size, pwv5_size, pwv6_size) != (6, 2, 3):
        raise ValueError(
            f"Unexpected waveform entry sizes: PWV4={pwv4_size}, PWV5={pwv5_size}, PWV6={pwv6_size}"
        )

    pwv7_section = two_ex.section(b"PWV7")
    if pwv7_section:
        _, pwv7_size, pwv7_count, _, pwv7 = two_ex.wave_entries(b"PWV7")
        if pwv7_size != 3:
            raise ValueError(f"Unexpected PWV7 entry size {pwv7_size}")
        detail_source, detail_count, detail_tag = pwv7, pwv7_count, "PWV7"
    else:
        detail_source, detail_count, detail_tag = pwv6, pwv6_count, "PWV6-resampled"

    for index in range(pwv4_count):
        mid, high, low = _sample_three_band(pwv6, pwv6_count, index, pwv4_count)
        red, green, blue, _ = _band_rgb(mid, high, low)
        offset = index * 6
        pwv4[offset + 3] = red
        pwv4[offset + 4] = green
        pwv4[offset + 5] = blue

    for index in range(pwv5_count):
        mid, high, low = _sample_three_band(detail_source, detail_count, index, pwv5_count)
        red, green, blue, height = _band_rgb(mid, high, low)
        packed = (
            ((red * 7 // 255) << 13)
            | ((green * 7 // 255) << 10)
            | ((blue * 7 // 255) << 7)
            | ((height * 31 // 255) << 2)
        )
        struct.pack_into(">H", pwv5, index * 2, packed)

    adapted = ext.to_bytes()
    if len(adapted) != len(ext_data):
        raise AssertionError("Waveform adaptation changed EXT file size")
    return adapted, {
        "pwv4_entries": pwv4_count,
        "pwv5_entries": pwv5_count,
        "pwv6_entries": pwv6_count,
        "detail_source": detail_tag,
        "detail_entries": detail_count,
    }
