# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ken(github.com/krovma)

"""Render one or more KGBR Game Boy Printer images as a grayscale PNG."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


HEADER_SIZE = 12
LEGACY_HEADER_SIZE = 8
MAGIC = b"KGBR"
VERSION = 3
BLOCK_VERSION = 2
LEGACY_VERSION = 1
FILE_HEADER_SIZE = 6
BLOCK_HEADER_SIZE = 8
DEFAULT_PALETTE = 0xE4
DEFAULT_EXPOSURE = 0x7F
MARGIN_UNIT_HEIGHT = 8


class GBRFormatError(ValueError):
    """Raised when an input does not contain a supported KGBR image."""


@dataclass(frozen=True)
class GBRImage:
    width: int
    height: int
    pixels: bytes
    margin: int


def parse_palette(value: str) -> int:
    try:
        palette = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("palette must be an integer (for example, 0xE4)") from exc
    if not 0 <= palette <= 0xFF:
        raise argparse.ArgumentTypeError("palette must be between 0 and 255")
    return palette


def _decode_block(data: bytes, offset: int, version: int, palette: int | None) -> tuple[GBRImage, int]:
    if version not in (LEGACY_VERSION, BLOCK_VERSION):
        raise GBRFormatError(f"unsupported KGBR block version: {version}")
    header_size = BLOCK_HEADER_SIZE if version == BLOCK_VERSION else 4
    if len(data) - offset < header_size:
        raise GBRFormatError("file ends inside a KGBR block header")

    width = data[offset + 1]
    height = int.from_bytes(data[offset + 2:offset + 4], "little")
    margin = data[offset + 5] if version == BLOCK_VERSION else 0
    file_palette = data[offset + 6] if version == BLOCK_VERSION else DEFAULT_PALETTE
    exposure = (data[offset + 7] & 0x7F) if version == BLOCK_VERSION else DEFAULT_EXPOSURE
    active_palette = file_palette if palette is None else palette
    darkest = 0x7F - exposure
    gray_levels = tuple((255 * (3 - shade) + darkest * shade + 1) // 3 for shade in range(4))
    if width == 0 or width % 8:
        raise GBRFormatError(f"width must be a positive multiple of 8, got {width}")
    if height == 0 or height % 8:
        raise GBRFormatError(f"height must be a positive multiple of 8, got {height}")

    payload_start = offset + header_size
    payload_end = payload_start + width * height // 4
    if payload_end > len(data):
        raise GBRFormatError(f"block needs {payload_end} bytes, got {len(data)}")

    pixels = bytearray(width * height)
    tile_columns = width // 8
    tile_rows = height // 8
    tile_data = memoryview(data)[payload_start:payload_end]
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
                    shade = (active_palette >> (color * 2)) & 0x03
                    pixels[output_offset + x] = gray_levels[shade]
    return GBRImage(width, height, bytes(pixels), margin), payload_end


def decode_gbr(data: bytes, palette: int | None = None) -> list[GBRImage]:
    if len(data) < LEGACY_HEADER_SIZE:
        if len(data) < FILE_HEADER_SIZE or data[:5] != MAGIC + bytes((VERSION,)):
            raise GBRFormatError("file is shorter than the KGBR header")
    if data[:4] != MAGIC:
        raise GBRFormatError("invalid KGBR signature")
    version = data[4]
    if version == VERSION:
        if len(data) < FILE_HEADER_SIZE or data[5] == 0:
            raise GBRFormatError("KGBR v3 requires at least one block")
        images = []
        offset = FILE_HEADER_SIZE
        for _ in range(data[5]):
            if offset >= len(data):
                raise GBRFormatError("file ends before a KGBR block")
            if data[offset] != BLOCK_VERSION:
                raise GBRFormatError(f"unsupported KGBR block version: {data[offset]}")
            image, offset = _decode_block(data, offset, data[offset], palette)
            images.append(image)
    elif version in (LEGACY_VERSION, BLOCK_VERSION):
        image, offset = _decode_block(data, 4, version, palette)
        images = [image]
    else:
        raise GBRFormatError(f"unsupported KGBR version: {data[4]}")
    if offset != len(data):
        raise GBRFormatError(f"expected {offset} bytes, got {len(data)}")
    return images


def merge_images(images: list[GBRImage]) -> tuple[int, int, bytes]:
    if not images:
        raise GBRFormatError("no KGBR blocks to merge")
    width = images[0].width
    pixels = bytearray()
    for image in images:
        if image.width != width:
            raise GBRFormatError("all input images must have the same width")
        before = (image.margin >> 4) * MARGIN_UNIT_HEIGHT
        after = (image.margin & 0x0F) * MARGIN_UNIT_HEIGHT
        pixels.extend(b"\xFF" * (width * before))
        pixels.extend(image.pixels)
        pixels.extend(b"\xFF" * (width * after))
    return width, len(pixels) // width, bytes(pixels)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, nargs="+", help="Input KGBR .gbr files, in merge order")
    parser.add_argument("-o", "--output", type=Path, help="Output PNG path")
    parser.add_argument(
        "--palette",
        type=parse_palette,
        help="Override the palette in every input file (decimal or 0x-prefixed)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_paths: list[Path] = args.input
    first_input = input_paths[0]
    default_output = (first_input.with_suffix(".png") if len(input_paths) == 1 else
                      first_input.with_name(f"{first_input.stem}-merged.png"))
    output_path: Path = args.output or default_output
    if any(input_path.resolve() == output_path.resolve() for input_path in input_paths):
        raise SystemExit("error: output path must differ from input path")
    try:
        images = [block for path in input_paths for block in decode_gbr(path.read_bytes(), args.palette)]
        if len(images) == 1:
            width, height, pixels = images[0].width, images[0].height, images[0].pixels
        else:
            width, height, pixels = merge_images(images)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with Image.frombytes("L", (width, height), pixels) as image:
            image.save(output_path, format="PNG")
    except (OSError, GBRFormatError) as exc:
        raise SystemExit(f"error: {exc}") from exc
    print(f"rendered {len(input_paths)} file(s), {len(images)} block(s) ({width}x{height}) -> {output_path}")


if __name__ == "__main__":
    main()
