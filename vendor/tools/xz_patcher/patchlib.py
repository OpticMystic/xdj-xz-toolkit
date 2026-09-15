"""Binary patch engine with prologue guards and atomic verification for Pioneer XDJ-XZ."""

import dataclasses
import pathlib
from typing import List, Sequence, Tuple, Union


@dataclasses.dataclass(frozen=True)
class Patch:
    offset: int
    expected: bytes
    replacement: bytes
    description: str

    def __post_init__(self):
        if len(self.expected) != len(self.replacement):
            raise ValueError(
                f"Patch length mismatch for '{self.description}': "
                f"expected {len(self.expected)} bytes, replacement is {len(self.replacement)} bytes"
            )


def apply_patches(binary: bytes, patches: Sequence[Patch]) -> bytes:
    """Apply a list of patches to binary after verifying all expected guards."""
    data = bytearray(binary)
    
    # 1. Guard check: ALL expected locations must match before ANY modification
    for p in patches:
        if p.offset + len(p.expected) > len(data):
            raise IndexError(f"Patch '{p.description}' out of bounds (offset {hex(p.offset)})")
        actual = bytes(data[p.offset:p.offset + len(p.expected)])
        if actual != p.expected:
            if actual == p.replacement:
                # Already patched
                continue
            raise ValueError(
                f"Guard failed for '{p.description}' at offset {hex(p.offset)}:\n"
                f"  Expected: {p.expected.hex()}\n"
                f"  Actual:   {actual.hex()}"
            )
            
    # 2. Apply modifications
    for p in patches:
        data[p.offset:p.offset + len(p.replacement)] = p.replacement
        
    return bytes(data)


def revert_patches(binary: bytes, patches: Sequence[Patch]) -> bytes:
    """Revert a list of patches by swapping expected and replacement."""
    reversed_patches = [
        Patch(
            offset=p.offset,
            expected=p.replacement,
            replacement=p.expected,
            description=f"Revert {p.description}",
        )
        for p in patches
    ]
    return apply_patches(binary, reversed_patches)
