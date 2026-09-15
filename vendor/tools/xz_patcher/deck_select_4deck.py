"""4-Deck Standalone USB Playback patch for Pioneer DJ XDJ-XZ (firmware 1.26).

Enables full 4-deck switching (Deck 1 <-> 3, Deck 2 <-> 4) in standalone USB mode
via double-tap SHIFT or on-screen Deck Select, unlocking 4-deck USB playback.
"""

from typing import List
from .patchlib import Patch, apply_patches, revert_patches

PATCHES_1_26: List[Patch] = [
    # In IsAbleDeckSelect at file offset 0xd1fb8 (VA 0xd9fb8)
    # Original: add r0, r0, r0, lsl #1; movw r3, #0x7cac (checks if in PC Rekordbox/Serato mode)
    # Replacement: mov r0, #1; bx lr (unconditionally enable 4-deck switching in standalone USB mode)
    Patch(
        offset=0xd1fb8,
        expected=bytes.fromhex("800080e0ac3c07e3"),
        replacement=bytes.fromhex("0100a0e31eff2fe1"),
        description="Enable 4-Deck standalone USB switching in IsAbleDeckSelect",
    ),
]


def patch_rbp(binary: bytes) -> bytes:
    """Apply 4-deck standalone USB patch to rbp binary."""
    return apply_patches(binary, PATCHES_1_26)


def unpatch_rbp(binary: bytes) -> bytes:
    """Revert 4-deck standalone USB patch."""
    return revert_patches(binary, PATCHES_1_26)
