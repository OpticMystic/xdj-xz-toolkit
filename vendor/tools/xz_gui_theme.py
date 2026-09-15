"""Build a reversible custom XDJ-XZ GUI image pack."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_gui.theme import build_theme


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("stock_pack", type=pathlib.Path)
    parser.add_argument("output_pack", type=pathlib.Path)
    parser.add_argument("--preview-dir", type=pathlib.Path, required=True)
    parser.add_argument("--font", type=pathlib.Path)
    parser.add_argument("--logo", type=pathlib.Path)
    args = parser.parse_args()
    build_theme(args.stock_pack, args.output_pack, args.preview_dir, args.font, args.logo)
    digest = hashlib.md5(args.output_pack.read_bytes()).hexdigest()
    print(f"{args.output_pack.resolve()} bytes={args.output_pack.stat().st_size} md5={digest}")


if __name__ == "__main__":
    main()
