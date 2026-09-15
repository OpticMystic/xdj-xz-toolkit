# SPDX-License-Identifier: MIT
"""Publish immutable, discoverable DJ branding bundles on a selected volume."""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import tempfile
from urllib.parse import urlsplit
import uuid

from .cache import _regular, _safe_path

MAX_FILES = 64
MAX_FILE_BYTES = 2 * 1024**3
MAX_TOTAL_BYTES = 4 * 1024**3
EXTENSIONS = {
    'logo': {'.png', '.jpg', '.jpeg', '.webp'},
    'epk': {'.pdf', '.txt'},
    'media': {'.png', '.jpg', '.jpeg', '.webp', '.mp4', '.mov', '.webm'},
}


def import_branding(request, job):
    volume = _safe_path(request['volume'])
    if not volume.is_dir():
        raise ValueError('Choose an existing USB or staging folder')
    artist = request.get('artist', '').strip()
    if not artist or len(artist) > 160 or any(ord(c) < 32 for c in artist):
        raise ValueError('Artist name must contain 1 to 160 printable characters')
    website = request.get('website', '').strip()
    if website:
        url = urlsplit(website)
        if (url.scheme not in ('http', 'https') or not url.hostname or url.username or
                url.password or len(website) > 2048 or any(c.isspace() for c in website)):
            raise ValueError('Website must be a public HTTP or HTTPS URL without credentials')
    entries = request.get('files')
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_FILES:
        raise ValueError('Choose 1 to 64 branding files')
    inputs = []
    for entry in entries:
        role = entry.get('role')
        if role not in EXTENSIONS:
            raise ValueError('Branding role must be logo, epk or media')
        source = _regular(entry['path'])
        if source.suffix.lower() not in EXTENSIONS[role]:
            raise ValueError('Unsupported file type for ' + role)
        size = source.stat().st_size
        if not 0 < size <= MAX_FILE_BYTES:
            raise ValueError('Each branding file must be nonempty and at most 2 GiB')
        inputs.append((role, source, size))
    if sum(size for _, _, size in inputs) > MAX_TOTAL_BYTES:
        raise ValueError('A branding bundle must be at most 4 GiB')
    job.check()
    root = _safe_path(volume / 'VJ.Tools' / 'Branding' / 'bundles')
    root.mkdir(parents=True, exist_ok=True)
    bundle_id = str(uuid.uuid4())
    destination = root / bundle_id
    with tempfile.TemporaryDirectory(prefix='.import-', dir=root) as temporary:
        stage = Path(temporary)
        (stage / 'assets').mkdir()
        files = []
        for index, (role, source, expected_size) in enumerate(inputs):
            job.progress('branding', 'Copying branding file ' + str(index + 1) + ' of ' + str(len(inputs)))
            safe_name = re.sub(r'[^a-zA-Z0-9._-]', '_', source.stem)[:80] or 'asset'
            relative = 'assets/' + f'{index + 1:03d}-' + safe_name + source.suffix.lower()
            before = source.stat()
            digest = hashlib.sha256()
            copied = 0
            with source.open('rb') as incoming, (stage / relative).open('xb') as outgoing:
                while block := incoming.read(1024 * 1024):
                    job.check()
                    copied += len(block)
                    if copied > expected_size:
                        raise ValueError('Branding source changed while copying')
                    outgoing.write(block)
                    digest.update(block)
            after = source.stat()
            if copied != expected_size or (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                raise ValueError('Branding source changed while copying')
            files.append({'role': role, 'path': relative, 'name': source.name,
                          'bytes': copied, 'sha256': digest.hexdigest()})
        manifest = {'schema': 'vj.tools.dj-branding', 'version': 1, 'id': bundle_id,
                    'artist': artist, 'website': website or None,
                    'created_at': datetime.now(timezone.utc).isoformat(), 'files': files}
        (stage / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
        job.check()
        # UUID paths publish once. An existing destination is never replaced.
        if destination.exists():
            raise FileExistsError('Branding destination already exists')
        stage.rename(destination)
    return {'branding_directory': str(destination), 'manifest': str(destination / 'manifest.json'),
            'artist': artist, 'files': len(files), 'network_pickup_implemented': False}
