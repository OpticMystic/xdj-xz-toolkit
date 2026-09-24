"""Prepare XZ Mods loading artwork in a local GUI pack; never contact hardware."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'vendor'))
from tools.xz_gui.image_pack import ImagePack
from tools.xz_gui.theme import build_theme


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-pack', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--preview-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Choose a new output path; existing packs are never overwritten')
    original = ImagePack.read(args.stock_pack)
    build_theme(args.stock_pack, args.output, args.preview_dir,
                logo_path=ROOT / 'mods/ui/assets/xz-mods.png')
    result = ImagePack.read(args.output)
    before, after = original.to_bytes(), result.to_bytes()
    if original.entries != result.entries or len(before) != len(after):
        raise ValueError('Branding changed image-pack structure')
    allowed = bytearray(before)
    for index in (1446, 1487):
        entry = original.entries[index]
        start, end = entry.data_offset, entry.data_offset + entry.pixel_size
        allowed[start:end] = after[start:end]
        result.image(index).save(args.preview_dir / f'packed-{index}.png')
    if allowed != after:
        raise ValueError('Branding changed unrelated graphics or padding')
    report = {'changed_images': [1446, 1487], 'unchanged_structure_and_other_pixels': True,
              'input_sha256': hashlib.sha256(before).hexdigest(),
              'output_sha256': hashlib.sha256(after).hexdigest(),
              'hardware_verified': False, 'deployment': 'not deployed'}
    (args.preview_dir / 'branding-proof.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
