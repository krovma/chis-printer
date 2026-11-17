# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ken(github.com/krovma)

"""Render a KGBR v1 Game Boy Printer image as a grayscale PNG."""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


HEADER_SIZE = 8
MAGIC = b"KGBR"
VERSION = 1
GRAY_LEVELS = (255, 170, 85, 0)


class GBRFormatError(ValueError):
    """Raised when an input does not contain a supported KGBR image."""


def parse_palette(value: str) -> int:
    try:
        palette = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("palette must be an integer (for example, 0xE4)") from exc
    if not 0 <= palette <= 0xFF:
        raise argparse.ArgumentTypeError("palette must be between 0 and 255")
    return palette


def decode_gbr(data: bytes, palette: int) -> tuple[int, int, bytes]:
    if len(data) < HEADER_SIZE:
        raise GBRFormatError("file is shorter than the 8-byte KGBR header")
    if data[:4] != MAGIC:
        raise GBRFormatError("invalid KGBR signature")
    if data[4] != VERSION:
        raise GBRFormatError(f"unsupported KGBR version: {data[4]}")

    width = data[5]
    height = int.from_bytes(data[6:8], "little")
    if width == 0 or width % 8:
        raise GBRFormatError(f"width must be a positive multiple of 8, got {width}")
    if height == 0 or height % 8:
        raise GBRFormatError(f"height must be a positive multiple of 8, got {height}")

    expected_size = HEADER_SIZE + width * height // 4
    if len(data) != expected_size:
        raise GBRFormatError(f"expected {expected_size} bytes, got {len(data)}")

    pixels = bytearray(width * height)
    tile_columns = width // 8
    tile_rows = height // 8
    tile_data = memoryview(data)[HEADER_SIZE:]
    for tile_y in range(tile_rows):
        for tile_x in range(tile_columns):
            tile_offset = (tile_y * tile_columns + tile_x) * 16
            for row in range(8):
                low = tile_data[tile_offset + row * 2]
                high = tile_data[tile_offset + row * 2 + 1]
                output_offset = (tile_y * 8 + row) * width + tile_x * 8
                for x in range(8):
                    bit = 7 - x
                    color = ((low >> bit) & 1) | (((high >> bit) & 1) << 1)
                    shade = (palette >> (color * 2)) & 0x03
                    pixels[output_offset + x] = GRAY_LEVELS[shade]
    return width, height, bytes(pixels)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Input KGBR .gbr file")
    parser.add_argument("-o", "--output", type=Path, help="Output PNG path (default: input name with .png)")
    parser.add_argument(
        "--palette",
        type=parse_palette,
        default=0xE4,
        help="Game Boy palette register value, in decimal or with 0x prefix (default: 0xE4)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_path: Path = args.input
    output_path: Path = args.output or input_path.with_suffix(".png")
    if input_path.resolve() == output_path.resolve():
        raise SystemExit("error: output path must differ from input path")
    try:
        data = input_path.read_bytes()
        width, height, pixels = decode_gbr(data, args.palette)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with Image.frombytes("L", (width, height), pixels) as image:
            image.save(output_path, format="PNG")
    except (OSError, GBRFormatError) as exc:
        raise SystemExit(f"error: {exc}") from exc
    print(f"rendered {input_path} ({width}x{height}) -> {output_path}")


if __name__ == "__main__":
    main()
