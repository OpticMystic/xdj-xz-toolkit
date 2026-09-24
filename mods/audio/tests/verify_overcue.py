"""Exercise the real C decoder with valid and corrupt paged stem bundles."""
import argparse
import hashlib
import json
import math
import shlex
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def fixture(root, signal=False):
    source = root / 'Contents' / 'artist' / 'track.mp3'
    source.parent.mkdir(parents=True)
    source.write_bytes(b'test-source-identity')
    bundle = root / 'CDJMODS/stems/0123456789abcdef'
    bundle.mkdir(parents=True)
    track = {'file_path': '/Contents/artist/track.mp3', 'bundle': bundle.name}
    (root / 'CDJMODS/index.json').write_text(json.dumps({'schema': 'overcue-index/1', 'tracks': {'1': track}}))
    masks = {'drums': 1, 'harmonics': 2, 'vocal': 4, 'full-mix': 7,
             'instrumental': 3, 'vocals-drums': 5, 'vocals-harmonics': 6}
    if signal:
        parts = []
        for frequencies in ((193,), (557, 1297), (3719,)):
            parts.append([round(sum(3500 * math.sin(2 * math.pi * f * i / 96000 + f / 100) for f in frequencies)) for i in range(800017)])
    else:
        pcm = b''.join(struct.pack('<hh', i % 1000, -(i % 1000)) for i in range(32771))
    roles = {}
    drum_pages = drum_table = None
    for name, mask in masks.items():
        if signal:
            pcm = b''.join(struct.pack('<hh', *[sum(parts[r][i + channel * 17] for r in range(3) if mask & (1 << r)) for channel in range(2)]) for i in range(800000))
        pages = [pcm[i:i + 131072] for i in range(0, len(pcm), 131072)]
        compressed = [zlib.compress(p, level=0) for p in pages]
        offset = 24 + 48 * len(pages)
        table = b'OVPGZ001' + struct.pack('>IIQ', 131072, len(pages), len(pcm))
        for raw, part in zip(pages, compressed):
            table += struct.pack('>QII', offset, len(part), len(raw)) + hashlib.sha256(raw).digest()
            offset += len(part)
        (bundle / f'stems-sidecar-{name}.s16le.pgz').write_bytes(table + b''.join(compressed))
        roles[name] = {'loudness_gain': 1, 'bytes': len(pcm), 'page_table_sha256': hashlib.sha256(table).hexdigest()}
        if name == 'drums':
            drum_pages, drum_table = pages, table
        tag = b'PWV3' + struct.pack('>IIIII', 24, 29, 1, 5, 0) + bytes([0, 1, 2, 3, 255])
        (bundle / f'stems-{name}-waveform.EXT').write_bytes(b'PMAI' + struct.pack('>II', 12, 12 + len(tag)) + tag)
    manifest = {'schema': 'overcue-stems/4', 'runtime': {'sample_rate': 96000, 'channels': 2, 'format': 's16le', 'frames': len(pcm)//4},
                'source': {'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}, 'roles': roles}
    (bundle / 'overcue-manifest.json').write_text(json.dumps(manifest))
    return source, bundle, drum_pages, drum_table


def main():
    p = argparse.ArgumentParser(description=__doc__)
    compiler = p.add_mutually_exclusive_group(required=True)
    compiler.add_argument('--zig')
    compiler.add_argument('--cc')
    p.add_argument('--sanitizer', choices=['address', 'thread'])
    p.add_argument('--real-source', type=Path)
    args = p.parse_args()
    compiler = [str(Path(args.zig).resolve()), 'cc'] if args.zig else shlex.split(args.cc)
    checks = ['-std=c11', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation']
    if args.sanitizer:
        checks += ['-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-fsanitize=' + ('address,undefined' if args.sanitizer == 'address' else 'thread')]
    with tempfile.TemporaryDirectory(prefix='xz-overcue-') as folder:
        root = Path(folder)
        binary = root / 'test.exe'
        subprocess.run(compiler + checks + [
            '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS',
            str(ROOT / 'overcue_file.c'), str(ROOT / 'vendor/miniz/miniz_tinfl.c'),
            str(ROOT / 'vendor/sha256/sha256.c'), str(ROOT / 'tests/test_overcue_file.c'), '-lm', '-o', str(binary)], check=True)
        if args.real_source:
            subprocess.run([str(binary), str(args.real_source), 'real'], check=True)
        source, bundle, pages, table = fixture(root / 'usb')
        subprocess.run([str(binary), str(source), 'valid'], check=True)
        original = source.read_bytes()
        source.write_bytes(b'different-source')
        subprocess.run([str(binary), str(source), 'reject'], check=True)
        source.write_bytes(original)
        file = bundle / 'stems-sidecar-drums.s16le.pgz'
        valid = file.read_bytes()
        file.write_bytes(valid[:30] + bytes([valid[30] ^ 1]) + valid[31:])
        subprocess.run([str(binary), str(source), 'reject'], check=True)
        altered = bytes([pages[0][0] ^ 1]) + pages[0][1:]
        file.write_bytes(table + zlib.compress(altered, level=0) + zlib.compress(pages[1], level=0))
        subprocess.run([str(binary), str(source), 'corrupt'], check=True)
        streaming = root / 'stream.exe'
        subprocess.run(compiler + checks + [
            '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS',
            str(ROOT / 'overcue_file.c'), str(ROOT / 'overcue_stream.c'), str(ROOT / 'vendor/miniz/miniz_tinfl.c'),
            str(ROOT / 'vendor/sha256/sha256.c'), str(ROOT / 'tests/test_overcue_stream.c'), '-pthread', '-lm', '-o', str(streaming)], check=True)
        source, _, _, _ = fixture(root / 'stream-usb', signal=True)
        subprocess.run([str(streaming), str(source)], check=True, timeout=30)


if __name__ == '__main__':
    main()
