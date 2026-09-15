"""UI Watermark & Custom Firmware Indicator for Pioneer DJ XDJ-XZ."""

from typing import List
from .patchlib import Patch, apply_patches, revert_patches

PATCHES_1_26: List[Patch] = [
    # In model banner string at file offset 0x3e0fb0
    # Changes "XDJ-XZ\0" to "XZ-MOD\0" so the UI displays the custom firmware badge
    Patch(
        offset=0x3e0fb0,
        expected=b"XDJ-XZ\x00",
        replacement=b"XZ-MOD\x00",
        description="Display 'XZ-MOD' indicator on system UI banners",
    ),
]


def patch_rbp(binary: bytes) -> bytes:
    """Apply UI watermark patch to rbp binary."""
    return apply_patches(binary, PATCHES_1_26)


def unpatch_rbp(binary: bytes) -> bytes:
    """Revert UI watermark patch."""
    return revert_patches(binary, PATCHES_1_26)
