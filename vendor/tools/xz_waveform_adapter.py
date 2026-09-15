"""Convert Rekordbox PWV6/PWV7 3-band analysis for the XDJ-XZ RGB renderer."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_waveform.anlz import adapt_three_band_to_rgb


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("ext", type=pathlib.Path)
    parser.add_argument("two_ex", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    source = args.ext.read_bytes()
    adapted, metadata = adapt_three_band_to_rgb(source, args.two_ex.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(adapted)
    print(
        json.dumps(
            {
                **metadata,
                "bytes": len(adapted),
                "source_sha256": hashlib.sha256(source).hexdigest(),
                "output_sha256": hashlib.sha256(adapted).hexdigest(),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
