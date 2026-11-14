"""Generate C++ 4bpp icon bytes and 1bpp transparency masks from manifest.json.

The generated header keeps one variant table per semantic icon ID. This is
needed because the manifest intentionally contains several sizes for some IDs.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_MANIFEST = SCRIPT_DIR / "manifest.json"
DEFAULT_OUTPUT = SCRIPT_DIR / "generated_icons.h"
ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")


@dataclass(frozen=True)
class IconEntry:
    icon_id: str
    width: int
    height: int
    path: Path
    source_path: str
    pixels_4bpp: bytes
    mask_1bpp: bytes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="Manifest JSON path")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="Generated C++ header path")
    parser.add_argument("--alpha-threshold", type=int, default=128, help="Alpha >= threshold is drawable (0..255)")
    return parser.parse_args()


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def load_manifest(manifest_path: Path, alpha_threshold: int) -> list[IconEntry]:
    if not 0 <= alpha_threshold <= 255:
        fail("--alpha-threshold must be between 0 and 255")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except OSError as exc:
        fail(f"cannot read manifest {manifest_path}: {exc}")
    except json.JSONDecodeError as exc:
        fail(f"invalid manifest JSON: {exc}")
    records = manifest.get("icons") if isinstance(manifest, dict) else None
    if not isinstance(records, list) or not records:
        fail("manifest must contain a non-empty 'icons' list")

    root = manifest_path.resolve().parent
    entries: list[IconEntry] = []
    seen_paths: set[Path] = set()
    seen_variants: set[tuple[str, int]] = set()
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            fail(f"icons[{index}] must be an object")
        icon_id = record.get("id")
        source_path = record.get("path")
        size = record.get("size")
        if not isinstance(icon_id, str) or not ID_RE.fullmatch(icon_id):
            fail(f"icons[{index}].id must match {ID_RE.pattern!r}")
        if not isinstance(source_path, str) or not source_path:
            fail(f"icons[{index}].path must be a non-empty string")
        if not isinstance(size, list) or len(size) != 2 or not all(isinstance(value, int) for value in size):
            fail(f"icons[{index}].size must be [width, height]")
        width, height = size
        if width <= 0 or height <= 0 or width > 65535 or height > 65535:
            fail(f"icons[{index}].size must be within 1..65535")
        if width % 2:
            fail(f"icons[{index}] width {width} is odd; 4bpp packing requires an even width")
        variant_key = (icon_id, width)
        if variant_key in seen_variants:
            fail(f"manifest repeats the same id/width variant: {icon_id} {width}px")
        seen_variants.add(variant_key)
        image_path = (root / source_path).resolve()
        try:
            image_path.relative_to(root)
        except ValueError:
            fail(f"icons[{index}].path escapes the manifest directory: {source_path}")
        if image_path in seen_paths:
            fail(f"manifest lists the same image more than once: {source_path}")
        seen_paths.add(image_path)
        if not image_path.is_file():
            fail(f"icons[{index}] image does not exist: {source_path}")

        try:
            with Image.open(image_path) as image:
                if image.size != (width, height):
                    fail(f"{source_path} is {image.size}, manifest says {(width, height)}")
                rgba = image.convert("RGBA")
                pixels_4bpp, mask_1bpp = encode_pixels(rgba, alpha_threshold)
        except OSError as exc:
            fail(f"cannot read {source_path}: {exc}")
        entries.append(IconEntry(icon_id, width, height, image_path, source_path, pixels_4bpp, mask_1bpp))
    return entries


def encode_pixels(image: Image.Image, alpha_threshold: int) -> tuple[bytes, bytes]:
    width, height = image.size
    pixels = bytearray(width * height // 2)
    mask_row_stride = (width + 7) // 8
    mask = bytearray(mask_row_stride * height)
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = image.getpixel((x, y))
            pixel_index = y * width + x
            if alpha >= alpha_threshold:
                # Integer BT.709-ish luma, then quantize to the 16 Gray4 levels.
                luma = (54 * red + 183 * green + 19 * blue + 128) // 256
                gray4 = (luma * 15 + 127) // 255
                mask_index = y * mask_row_stride + (x // 8)
                mask[mask_index] |= 0x80 >> (x & 7)
            else:
                gray4 = 0
            byte_index = pixel_index // 2
            if pixel_index & 1:
                pixels[byte_index] |= gray4
            else:
                pixels[byte_index] = gray4 << 4
    return bytes(pixels), bytes(mask)


def cpp_id(icon_id: str) -> str:
    return icon_id.upper()


def bytes_literal(data: bytes, indent: str = "    ") -> str:
    if not data:
        return f"{indent}/* empty */"
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append(indent + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return "\n".join(lines)


def write_array(lines: list[str], name: str, data: bytes) -> None:
    lines.append(f"inline constexpr std::uint8_t {name}[] = {{")
    lines.append(bytes_literal(data))
    lines.append("};")
    lines.append("")


def generate_header(entries: list[IconEntry], manifest_path: Path, alpha_threshold: int) -> str:
    groups: OrderedDict[str, list[IconEntry]] = OrderedDict()
    for entry in entries:
        groups.setdefault(entry.icon_id, []).append(entry)

    lines = [
        "// Generated by resource/icon/generate_icon_header.py; do not edit by hand.",
        f"// Source manifest: {manifest_path.name}; alpha threshold: {alpha_threshold}",
        "// Pixel bytes: row-major 4bpp, pixel 0 in the high nibble, pixel 1 in the low nibble.",
        "// Mask bytes: row-major 1bpp, pixel 0 in bit 7 (MSB-first); 1 means alpha is drawable.",
        "// One ID may have several sizes. ICON_<ID> and ICON_<ID>_MASK are parallel variant tables.",
        "",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace kuper::icons {",
        "",
        "struct IconBitmap4bpp {",
        "    std::uint16_t width;",
        "    std::uint16_t height;",
        "    std::uint16_t row_stride;",
        "    const std::uint8_t* bytes;",
        "    std::size_t byte_count;",
        "};",
        "",
        "struct IconMask1bpp {",
        "    std::uint16_t width;",
        "    std::uint16_t height;",
        "    std::uint16_t row_stride;",
        "    const std::uint8_t* bytes;",
        "    std::size_t byte_count;",
        "};",
        "",
    ]

    for entry in entries:
        base = f"ICON_{cpp_id(entry.icon_id)}_{entry.width}"
        write_array(lines, f"{base}_DATA", entry.pixels_4bpp)
        write_array(lines, f"{base}_MASK_DATA", entry.mask_1bpp)

    for icon_id, variants in groups.items():
        upper = cpp_id(icon_id)
        lines.append(f"inline constexpr IconBitmap4bpp ICON_{upper}[] = {{")
        for entry in variants:
            base = f"ICON_{upper}_{entry.width}"
            pixel_stride = entry.width // 2
            lines.append(f"    {{{entry.width}, {entry.height}, {pixel_stride}, {base}_DATA, sizeof({base}_DATA)}},")
        lines.extend(["};", f"inline constexpr std::size_t ICON_{upper}_COUNT = sizeof(ICON_{upper}) / sizeof(ICON_{upper}[0]);", ""])

        lines.append(f"inline constexpr IconMask1bpp ICON_{upper}_MASK[] = {{")
        for entry in variants:
            base = f"ICON_{upper}_{entry.width}"
            mask_stride = (entry.width + 7) // 8
            lines.append(f"    {{{entry.width}, {entry.height}, {mask_stride}, {base}_MASK_DATA, sizeof({base}_MASK_DATA)}},")
        lines.extend(["};", f"inline constexpr std::size_t ICON_{upper}_MASK_COUNT = sizeof(ICON_{upper}_MASK) / sizeof(ICON_{upper}_MASK[0]);", ""])

    lines.extend(["}  // namespace kuper::icons", ""])
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    manifest_path = args.manifest.resolve()
    entries = load_manifest(manifest_path, args.alpha_threshold)
    header = generate_header(entries, manifest_path, args.alpha_threshold)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="ascii", newline="\n")
    print(json.dumps({"manifest": str(manifest_path), "output": str(args.output), "images": len(entries), "ids": len({entry.icon_id for entry in entries}), "header_bytes": len(header.encode("ascii")), "alpha_threshold": args.alpha_threshold}))


if __name__ == "__main__":
    main()
