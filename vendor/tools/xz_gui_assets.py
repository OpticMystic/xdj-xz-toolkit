"""CLI for extracting and validating XDJ-XZ imagedata.dat assets."""

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = pathlib.Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tools.xz_gui.image_pack import ImagePack


def make_contact_sheet(pack: ImagePack, indices: list[int], output: pathlib.Path) -> None:
    cell_width, cell_height = 260, 190
    columns = 4
    rows = (len(indices) + columns - 1) // columns
    sheet = Image.new("RGB", (cell_width * columns, cell_height * rows), "#111317")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for position, index in enumerate(indices):
        entry = pack.entries[index]
        image = pack.image(index)
        image.thumbnail((cell_width - 16, cell_height - 36))
        x = (position % columns) * cell_width + (cell_width - image.width) // 2
        y = (position // columns) * cell_height + 22 + (cell_height - 34 - image.height) // 2
        sheet.paste(image, (x, y))
        draw.text(
            ((position % columns) * cell_width + 6, (position // columns) * cell_height + 5),
            f"#{index} {entry.width}x{entry.height}",
            fill="#ffb000",
            font=font,
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack", type=pathlib.Path)
    sub = parser.add_subparsers(dest="command", required=True)
    list_parser = sub.add_parser("list")
    list_parser.add_argument("--min-area", type=int, default=0)
    extract_parser = sub.add_parser("extract")
    extract_parser.add_argument("indices", nargs="+", type=int)
    extract_parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    sheet_parser = sub.add_parser("contact-sheet")
    sheet_parser.add_argument("indices", nargs="+", type=int)
    sheet_parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    pack = ImagePack.read(args.pack)
    if args.command == "list":
        for entry in pack.entries:
            if entry.width * entry.height >= args.min_area:
                print(
                    f"{entry.index:4d} {entry.width:4d}x{entry.height:<4d} "
                    f"offset=0x{entry.data_offset:08x} padding={entry.padding_size}"
                )
    elif args.command == "extract":
        args.output_dir.mkdir(parents=True, exist_ok=True)
        for index in args.indices:
            entry = pack.entries[index]
            pack.image(index).save(args.output_dir / f"{index:04d}-{entry.width}x{entry.height}.png")
    else:
        make_contact_sheet(pack, args.indices, args.output)


if __name__ == "__main__":
    main()
