"""Unquantized Immediate Beat Jump patch for Pioneer DJ XDJ-XZ (firmware 1.26)."""

from typing import List
from .patchlib import Patch, apply_patches, revert_patches

PATCHES_1_26: List[Patch] = [
    # In playengine::Player::playBeatJump at offset 0x5e3a0 (VA 0x663a0)
    # Original: cmp r6, #1; beq 0x66494 -> checks quantize mode
    # Bypass quantize delay queue to execute beat jump instantaneously on pad hit
    Patch(
        offset=0x5e3a0,
        expected=bytes.fromhex("010056e33a00000a"),
        replacement=bytes.fromhex("0000a0e13a0000ea"),
        description="Instant unquantized beat jump response on pad press",
    ),
]


def patch_rbp(binary: bytes) -> bytes:
    """Apply unquantized beat jump patch to rbp binary."""
    return apply_patches(binary, PATCHES_1_26)


def unpatch_rbp(binary: bytes) -> bytes:
    """Revert unquantized beat jump patch."""
    return revert_patches(binary, PATCHES_1_26)
