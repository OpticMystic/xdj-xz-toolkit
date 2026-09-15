"""Build and verify encrypted autoexec.bin images for Pioneer DJ XDJ-XZ."""

import io
import os
import pathlib
import struct
import tempfile
from typing import Optional, Union

import pycdlib
from .crypto import SECTOR, ISO_BLOCK, crypt_sectors, load_key

ISO_PADDING_BLOCKS = 150


def build_iso_from_directory(source_dir: Union[str, pathlib.Path], volume_ident: str = "UsbAuto") -> bytes:
    """Build a Rock Ridge ISO 9660 image from a directory structure."""
    source = pathlib.Path(source_dir)
    autoexec_script = source / "autoexec.sh"
    if not autoexec_script.is_file():
        raise FileNotFoundError(f"Missing required entrypoint: {autoexec_script}")

    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, vol_ident=volume_ident, rock_ridge="1.09")
    
    iso_directories = {pathlib.Path(): ""}
    dir_count = 0
    file_count = 0

    # Add directories first in sorted order
    for path in sorted((p for p in source.rglob("*") if p.is_dir())):
        relative = path.relative_to(source)
        parent_iso = iso_directories[relative.parent]
        dir_count += 1
        iso_path = f"{parent_iso}/D{dir_count:07d}"
        iso.add_directory(
            iso_path=iso_path,
            rr_name=relative.name,
            file_mode=0o40555,
        )
        iso_directories[relative] = iso_path

    # Add files
    for path in sorted((p for p in source.rglob("*") if p.is_file())):
        relative = path.relative_to(source)
        parent_iso = iso_directories[relative.parent]
        file_count += 1
        iso_path = f"{parent_iso}/F{file_count:07d};1"
        is_exec = path.suffix in (".sh", ".so") or path.name in ("rbp", "autoexec.sh", "with_core_app")
        mode = 0o100755 if is_exec else 0o100644
        iso.add_file(
            str(path),
            iso_path=iso_path,
            rr_name=relative.name,
            file_mode=mode,
        )

    out_stream = io.BytesIO()
    iso.write_fp(out_stream)
    iso.close()
    
    image = bytearray(out_stream.getvalue())
    image.extend(bytes(ISO_PADDING_BLOCKS * ISO_BLOCK))

    # Update PVD block count for mkisofs compatibility
    volume_blocks = len(image) // ISO_BLOCK
    pvd = 16 * ISO_BLOCK
    struct.pack_into("<I", image, pvd + 80, volume_blocks)
    struct.pack_into(">I", image, pvd + 84, volume_blocks)
    
    if len(image) % SECTOR != 0:
        image.extend(b"\0" * (SECTOR - (len(image) % SECTOR)))
        
    return bytes(image)


def build_autoexec_bin(source_dir: Union[str, pathlib.Path], output_bin_path: Union[str, pathlib.Path], key_path: Union[str, pathlib.Path]) -> int:
    """Build, encrypt and write an autoexec.bin file ready for USB root."""
    key = load_key(key_path)
    plain_iso = build_iso_from_directory(source_dir)
    encrypted = crypt_sectors(plain_iso, key, decrypt=False)
    pathlib.Path(output_bin_path).write_bytes(encrypted)
    return len(encrypted)


def verify_autoexec_bin(bin_path: Union[str, pathlib.Path], key_path: Union[str, pathlib.Path]) -> dict:
    """Verify an encrypted autoexec.bin image and inspect its ISO metadata."""
    key = load_key(key_path)
    raw = pathlib.Path(bin_path).read_bytes()
    if len(raw) % SECTOR != 0:
        raise ValueError(f"Image {bin_path} is not sector-aligned")
        
    plain = crypt_sectors(raw, key, decrypt=True)
    pvd_offset = 64 * SECTOR
    pvd = plain[pvd_offset:pvd_offset + 2048]
    if len(pvd) < 2048 or pvd[1:6] != b"CD001":
        raise ValueError("Invalid ISO 9660 signature in decrypted image - incorrect key or corrupted image")
        
    volume_name = pvd[40:72].decode("ascii", "replace").rstrip(" \0")
    return {
        "size_bytes": len(plain),
        "sectors": len(plain) // SECTOR,
        "volume_name": volume_name,
        "is_valid": True
    }


def extract_autoexec_file(
    bin_path: Union[str, pathlib.Path],
    key_path: Union[str, pathlib.Path],
    rr_path: str,
) -> bytes:
    """Decrypt an autoexec image and return one Rock Ridge file by path."""
    key = load_key(key_path)
    raw = pathlib.Path(bin_path).read_bytes()
    plain = crypt_sectors(raw, key, decrypt=True)
    iso = pycdlib.PyCdlib()
    iso.open_fp(io.BytesIO(plain))
    output = io.BytesIO()
    try:
        iso.get_file_from_iso_fp(output, rr_path=rr_path)
    finally:
        iso.close()
    return output.getvalue()
