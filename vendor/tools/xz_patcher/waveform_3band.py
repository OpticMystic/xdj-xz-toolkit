"""Force the XDJ-XZ firmware's native PWV4/PWV5 color path (mode 3).

Important format distinction:
  * PWV4/PWV5 are the RGB color preview/detail atoms parsed by firmware 1.26.
  * PWV6/PWV7 are true 3-band atoms and require adaptation before this firmware
    can consume them.

The previous five-point patch forced mode 2 and changed an HID extent-number
branch. All real renderer checks compare against mode 3; the extent patch
corrupted first-chunk handling. This corrected patch changes only the getter.
"""

from typing import List

from .patchlib import Patch, apply_patches, revert_patches


PATCHES_1_26: List[Patch] = [
    Patch(
        offset=0x132D0C,
        expected=bytes.fromhex("440050e30100a083"),
        replacement=bytes.fromhex("0300a0e31eff2fe1"),
        description="Force native mode 3 PWV4/PWV5 color waveform path",
    ),
]


def patch_rbp(binary: bytes) -> bytes:
    return apply_patches(binary, PATCHES_1_26)


def unpatch_rbp(binary: bytes) -> bytes:
    return revert_patches(binary, PATCHES_1_26)
