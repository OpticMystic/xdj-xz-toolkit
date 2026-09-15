"""AES-256-CBC sector-by-sector cryptoloop implementation for Pioneer DJ XDJ-XZ.

Matches the kernel cryptoloop and decrypt_autoexec.sh implementation on Pioneer hardware.
"""

import io
import pathlib
import struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

SECTOR = 512
ISO_BLOCK = 2048


def load_key(path_or_bytes):
    """Load and normalize AES-256 key from a file path or raw bytes.
    
    Pioneer's xstrncpy copies at most 31 bytes and writes a terminating NUL byte,
    yielding the effective 32-byte AES key.
    """
    if isinstance(path_or_bytes, (str, pathlib.Path)):
        lines = pathlib.Path(path_or_bytes).read_bytes().splitlines()
        if not lines:
            raise ValueError(f"Key file {path_or_bytes} is empty")
        raw = lines[0]
    elif isinstance(path_or_bytes, bytes):
        raw = path_or_bytes.splitlines()[0] if b"\n" in path_or_bytes else path_or_bytes
    else:
        raise TypeError("path_or_bytes must be a path or bytes")
        
    return raw[:31].ljust(32, b"\0")


def crypt_sectors(body: bytes, key: bytes, decrypt: bool = False) -> bytes:
    """Apply AES-256-CBC independently to each 512-byte sector."""
    if len(body) % SECTOR != 0:
        raise ValueError(f"Payload length ({len(body)}) is not a multiple of sector size ({SECTOR})")
        
    algorithm = algorithms.AES(key)
    output = bytearray(len(body))
    
    for sector, offset in enumerate(range(0, len(body), SECTOR)):
        iv = struct.pack("<I", sector & 0xFFFFFFFF) + bytes(12)
        cipher = Cipher(algorithm, modes.CBC(iv))
        op = cipher.decryptor() if decrypt else cipher.encryptor()
        block = body[offset:offset + SECTOR]
        output[offset:offset + SECTOR] = op.update(block) + op.finalize()
        
    return bytes(output)


def decrypt_file(input_path, output_path, key_path):
    key = load_key(key_path)
    encrypted = pathlib.Path(input_path).read_bytes()
    # If trailer is present (like in .UPD files), slice to 512-byte alignment
    aligned_len = (len(encrypted) // SECTOR) * SECTOR
    plain = crypt_sectors(encrypted[:aligned_len], key, decrypt=True)
    pathlib.Path(output_path).write_bytes(plain)
    return len(plain)


def encrypt_file(input_path, output_path, key_path):
    key = load_key(key_path)
    plain = pathlib.Path(input_path).read_bytes()
    if len(plain) % SECTOR != 0:
        plain += b"\0" * (SECTOR - (len(plain) % SECTOR))
    encrypted = crypt_sectors(plain, key, decrypt=False)
    pathlib.Path(output_path).write_bytes(encrypted)
    return len(encrypted)
