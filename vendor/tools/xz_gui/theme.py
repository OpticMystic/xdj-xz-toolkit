"""Build the initial reversible XZ custom theme from a stock image pack."""

from __future__ import annotations

import pathlib

from PIL import Image, ImageDraw, ImageFont

from .image_pack import ImagePack


SPLASH_IMAGE_ID = 1446
UNLOADED_LOGO_IMAGE_ID = 1487
AMBER = (255, 170, 0)
WHITE = (245, 245, 245)
BLACK = (0, 0, 0)


def _font(size: int, preferred_font: pathlib.Path | None = None) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        preferred_font,
        pathlib.Path("C:/Windows/Fonts/segoeuib.ttf"),
        pathlib.Path("C:/Windows/Fonts/arialbd.ttf"),
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def _centered(draw: ImageDraw.ImageDraw, text: str, y: int, font, fill, width: int) -> None:
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(((width - (box[2] - box[0])) // 2, y), text, font=font, fill=fill)


def make_splash(preferred_font: pathlib.Path | None = None) -> Image.Image:
    image = Image.new("RGB", (800, 480), BLACK)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 799, 479), outline=AMBER, width=5)
    draw.rounded_rectangle((74, 104, 726, 365), radius=24, outline=AMBER, width=5, fill=(6, 8, 10))
    _centered(draw, "XDJ-XZ", 135, _font(72, preferred_font), WHITE, 800)
    _centered(draw, "CUSTOM RAM UI", 230, _font(43, preferred_font), AMBER, 800)
    _centered(draw, "REBOOT RESTORES STOCK", 306, _font(24, preferred_font), WHITE, 800)
    return image


def make_unloaded_logo(
    preferred_font: pathlib.Path | None = None,
    logo_path: pathlib.Path | None = None,
) -> Image.Image:
    image = Image.new("RGB", (560, 92), BLACK)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((2, 2, 557, 89), radius=14, outline=AMBER, width=3, fill=(4, 6, 8))
    if logo_path is None:
        _centered(draw, "XZ CUSTOM UI", 17, _font(45, preferred_font), AMBER, 560)
        return image
    supplied = Image.open(logo_path).convert("RGBA")
    supplied.thumbnail((86, 86), Image.Resampling.LANCZOS)
    logo_x = 12 + (86 - supplied.width) // 2
    logo_y = 3 + (86 - supplied.height) // 2
    image.paste(supplied.convert("RGB"), (logo_x, logo_y))
    draw.text((112, 13), "VJ.TOOLS", font=_font(36, preferred_font), fill=AMBER)
    draw.text((114, 56), "RAM LOADER ACTIVE", font=_font(18, preferred_font), fill=WHITE)
    return image


def build_theme(
    stock_pack_path: pathlib.Path,
    output_pack_path: pathlib.Path,
    preview_dir: pathlib.Path,
    preferred_font: pathlib.Path | None = None,
    logo_path: pathlib.Path | None = None,
) -> None:
    pack = ImagePack.read(stock_pack_path)
    splash = make_splash(preferred_font)
    logo = make_unloaded_logo(preferred_font, logo_path)
    pack.replace(SPLASH_IMAGE_ID, splash)
    pack.replace(UNLOADED_LOGO_IMAGE_ID, logo)
    output_pack_path.parent.mkdir(parents=True, exist_ok=True)
    preview_dir.mkdir(parents=True, exist_ok=True)
    pack.write(output_pack_path)
    splash.save(preview_dir / "splash-1446.png")
    logo.save(preview_dir / "unloaded-logo-1487.png")
