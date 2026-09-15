"""Parser and same-dimension editor for XDJ-XZ imagedata.dat packs."""

from __future__ import annotations

import dataclasses
import pathlib
import struct

import numpy as np
from PIL import Image


ENTRY_SIZE = 44
PIXEL_FORMAT_RGB565 = 2


@dataclasses.dataclass(frozen=True)
class ImageEntry:
    index: int
    width: int
    height: int
    pixel_format: int
    table_offset: int
    data_offset: int
    stored_size: int
    pixel_size: int

    @property
    def padding_size(self) -> int:
        return self.stored_size - self.pixel_size


class ImagePack:
    """A validated image pack with a deliberately small editing interface."""

    def __init__(self, data: bytes):
        self._data = bytearray(data)
        if len(data) < ENTRY_SIZE:
            raise ValueError("Image pack is shorter than one table entry")
        first_data_offset = struct.unpack_from("<I", data, 32)[0]
        if first_data_offset % ENTRY_SIZE:
            raise ValueError("First image data offset is not table-aligned")
        self._count = first_data_offset // ENTRY_SIZE
        if self._count <= 0 or first_data_offset > len(data):
            raise ValueError("Invalid image table size")
        self._entries = self._parse_entries()

    @classmethod
    def read(cls, path: pathlib.Path | str) -> "ImagePack":
        return cls(pathlib.Path(path).read_bytes())

    @property
    def entries(self) -> tuple[ImageEntry, ...]:
        return self._entries

    def _parse_entries(self) -> tuple[ImageEntry, ...]:
        entries = []
        for index in range(self._count):
            offset = index * ENTRY_SIZE
            width, height = struct.unpack_from("<HH", self._data, offset + 4)
            pixel_format = struct.unpack_from("<I", self._data, offset + 24)[0]
            table_offset, data_offset, declared_total = struct.unpack_from("<III", self._data, offset + 28)
            next_data_offset = (
                struct.unpack_from("<I", self._data, offset + ENTRY_SIZE + 32)[0]
                if index + 1 < self._count
                else len(self._data)
            )
            if table_offset != offset:
                raise ValueError(f"Entry {index} table offset mismatch")
            if declared_total != len(self._data):
                raise ValueError(f"Entry {index} total-size guard mismatch")
            if not (self._count * ENTRY_SIZE <= data_offset <= next_data_offset <= len(self._data)):
                raise ValueError(f"Entry {index} data offsets are invalid")
            if pixel_format != PIXEL_FORMAT_RGB565:
                raise ValueError(f"Entry {index} has unsupported pixel format {pixel_format}")
            stored_size = next_data_offset - data_offset
            pixel_size = width * height * 2
            if stored_size < pixel_size:
                raise ValueError(f"Entry {index} pixel payload is truncated")
            entries.append(
                ImageEntry(
                    index=index,
                    width=width,
                    height=height,
                    pixel_format=pixel_format,
                    table_offset=table_offset,
                    data_offset=data_offset,
                    stored_size=stored_size,
                    pixel_size=pixel_size,
                )
            )
        return tuple(entries)

    def image(self, index: int) -> Image.Image:
        entry = self._entries[index]
        raw = memoryview(self._data)[entry.data_offset:entry.data_offset + entry.pixel_size]
        packed = np.frombuffer(raw, dtype="<u2").reshape((entry.height, entry.width))
        red = (((packed >> 11) & 31).astype(np.uint32) * 255 // 31).astype(np.uint8)
        green = (((packed >> 5) & 63).astype(np.uint32) * 255 // 63).astype(np.uint8)
        blue = ((packed & 31).astype(np.uint32) * 255 // 31).astype(np.uint8)
        return Image.fromarray(np.dstack((red, green, blue)), "RGB")

    def replace(self, index: int, image: Image.Image) -> None:
        entry = self._entries[index]
        rgb = image.convert("RGB")
        if rgb.size != (entry.width, entry.height):
            raise ValueError(
                f"Entry {index} requires {entry.width}x{entry.height}, received {rgb.width}x{rgb.height}"
            )
        pixels = np.asarray(rgb, dtype=np.uint32)
        packed = (
            ((pixels[:, :, 0] * 31 // 255) << 11)
            | ((pixels[:, :, 1] * 63 // 255) << 5)
            | (pixels[:, :, 2] * 31 // 255)
        ).astype("<u2")
        replacement = packed.tobytes()
        self._data[entry.data_offset:entry.data_offset + entry.pixel_size] = replacement

    def to_bytes(self) -> bytes:
        return bytes(self._data)

    def write(self, path: pathlib.Path | str) -> None:
        pathlib.Path(path).write_bytes(self.to_bytes())
