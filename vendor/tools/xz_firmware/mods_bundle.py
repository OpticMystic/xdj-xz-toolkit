"""Validate the standalone mod and display bridge as one versioned bundle."""
import hashlib
import json
from pathlib import Path


def load_mods_bundle(root: Path) -> dict:
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf8"))
    if (manifest.get("schema_version"), manifest.get("firmware"), manifest.get("profile")) != (1, "XDJ-XZ 1.26", "experimental"):
        raise ValueError("Unsupported XZ Mods bundle")
    required = {"libxz-mods.so", "libxz-receiver.so", "bootstrap.sh", "source.zip"}
    files = manifest.get("files", {})
    if not required.issubset(files):
        raise ValueError("XZ Mods bundle is missing the paired runtime, loader, or source")
    for relative, digest in files.items():
        path = (root / relative).resolve()
        if not path.is_relative_to(root.resolve()) or not path.is_file():
            raise ValueError(f"Missing XZ Mods bundle file: {relative}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"XZ Mods integrity check failed: {relative}")
    return manifest
