"""Validate the standalone mod and display bridge as one versioned bundle."""
import hashlib
import json
import zipfile
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


def verify_mods_source(root: Path, toolkit: Path) -> dict:
    manifest = load_mods_bundle(root)
    bootstrap = toolkit/'vendor/tools/xz_runtime/orchestrator.sh'
    if bootstrap.read_bytes().replace(b'\r\n', b'\n') != (root/'bootstrap.sh').read_bytes().replace(b'\r\n', b'\n'):
        raise ValueError('XZ runtime bootstrap is stale for current source')
    checked = 0
    with zipfile.ZipFile(root/'source.zip') as archive:
        for name in archive.namelist():
            relative = Path(name)
            if relative.suffix not in ('.c', '.h', '.ld'):
                continue
            source = (toolkit/relative).resolve()
            if not source.is_relative_to(toolkit.resolve()) or not source.is_file():
                raise ValueError('Missing compiled XZ source: '+name)
            if source.read_bytes().replace(b'\r\n', b'\n') != archive.read(name).replace(b'\r\n', b'\n'):
                raise ValueError('XZ runtime is stale for current source: '+name)
            checked += 1
    if not checked:
        raise ValueError('XZ runtime has no corresponding native source')
    return manifest
