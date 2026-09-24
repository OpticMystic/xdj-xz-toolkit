"""Verify the shipped checker and optional frozen backend against valid and damaged bundles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--checker', type=Path, required=True)
parser.add_argument('--backend', type=Path)
args = parser.parse_args()
toolkit = Path(__file__).resolve().parents[2]
fixture = runpy.run_path(str(toolkit/'mods/audio/tests/verify_overcue.py'))['fixture']


def check(source, expected=True):
    result = subprocess.run([str(args.checker.resolve()), str(source)], capture_output=True, text=True)
    assert (result.returncode == 0) == expected, result.stderr
    if expected:
        data = json.loads(result.stdout)
        assert data['verified_mixes'] == 7 and data['all_pages_verified']
    if args.backend:
        env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(args.checker.resolve().parent)}
        request = json.dumps({'method': 'inspect_overcue', 'source': str(source)})
        response = subprocess.run([str(args.backend.resolve())], input=request,
                                  capture_output=True, text=True, env=env, timeout=30)
        assert response.returncode == 0, response.stderr
        data = json.loads(response.stdout.splitlines()[-1])
        assert data['ok'] == expected, data


with tempfile.TemporaryDirectory(prefix='xz-overcue-builder-') as temporary:
    root = Path(temporary)/'USB café 音楽'
    source, bundle, _, _ = fixture(root)
    before = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in root.rglob('*') if p.is_file()}
    check(source)
    assert before == {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in root.rglob('*') if p.is_file()}
    original = source.read_bytes()
    source.write_bytes(b'wrong original track')
    check(source, False)
    source.write_bytes(original)
    page = bundle/'stems-sidecar-vocals-harmonics.s16le.pgz'
    valid = page.read_bytes()
    page.write_bytes(valid[:-1] + bytes([valid[-1] ^ 1]))
    check(source, False)
    page.write_bytes(valid)
    manifest_path = bundle/'overcue-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    manifest['schema'] = 'overcue-stems/999'
    manifest_path.write_text(json.dumps(manifest))
    check(source, False)
print('PASS OverCue v4: all seven mixes, every page, Unicode paths, no writes, wrong source, corrupt final page, unsupported schema')
