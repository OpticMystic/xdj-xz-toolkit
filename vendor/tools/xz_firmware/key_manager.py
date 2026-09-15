"""Key manager for XDJ-XZ AES-256 firmware keys."""

import pathlib
from typing import Optional

DEFAULT_KEY_PATHS = [
    pathlib.Path("keys/aes256.key"),
    pathlib.Path("gpl_source/initramfs/initramfs/usr/local/pdj/aes256.key"),
    pathlib.Path.home() / ".config/xdj-xz/aes256.key",
    pathlib.Path.home() / "AppData/Roaming/xdj-xz/aes256.key",
]


def find_default_key() -> Optional[pathlib.Path]:
    """Locate the default aes256.key file from standard locations."""
    for path in DEFAULT_KEY_PATHS:
        if path.is_file():
            return path.resolve()
    return None


def get_key_path(explicit_path: Optional[str] = None) -> pathlib.Path:
    """Resolve key path from explicit argument or default discovery."""
    if explicit_path:
        p = pathlib.Path(explicit_path)
        if not p.is_file():
            raise FileNotFoundError(f"Specified key file not found: {p}")
        return p.resolve()
        
    discovered = find_default_key()
    if not discovered:
        raise FileNotFoundError(
            "No aes256.key found. Please specify --key or place aes256.key in keys/ directory."
        )
    return discovered
