# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ken(github.com/krovma)

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


SCRIPT = Path(__file__).resolve().parents[1] / "resource" / "kgbr" / "gbr-to-png.py"
SPEC = importlib.util.spec_from_file_location("gbr_to_png", SCRIPT)
gbr_to_png = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = gbr_to_png
SPEC.loader.exec_module(gbr_to_png)

TILE = bytes((0x55, 0x33)) * 8


def gbr(version=2, width=8, control=(1, 0x13, 0xE4, 0x7F)):
    header = b"KGBR" + bytes((version, width)) + (8).to_bytes(2, "little")
    if version == 2:
        header += bytes(control)
    return header + TILE * (width // 8)


def gbr_v3(*blocks):
    return b"KGBR" + bytes((3, len(blocks))) + b"".join(block[4:] for block in blocks)


class GbrToPngTests(unittest.TestCase):
    def test_v2_palette_exposure_and_v1_compatibility(self):
        light_print = gbr(control=(1, 0x13, 0xE4, 0))
        dark_print = gbr(control=(1, 0x13, 0xE4, 0x7F))
        self.assertEqual(list(gbr_to_png.decode_gbr(light_print)[0].pixels[:4]), [255, 212, 170, 127])
        self.assertEqual(list(gbr_to_png.decode_gbr(dark_print)[0].pixels[:4]), [255, 170, 85, 0])
        self.assertEqual(list(gbr_to_png.decode_gbr(gbr(version=1))[0].pixels[:4]), [255, 170, 85, 0])
        self.assertEqual(list(gbr_to_png.decode_gbr(dark_print, 0x1B)[0].pixels[:4]), [0, 85, 170, 255])
        self.assertEqual(gbr_to_png.decode_gbr(gbr(control=(1, 0x13, 0xE4, 0xFF)))[0].pixels,
                         gbr_to_png.decode_gbr(dark_print)[0].pixels)

    def test_v3_block_versions_palette_and_sheets(self):
        first = gbr(control=(7, 0x12, 0xE4, 0x7F))
        second = gbr(control=(0, 0x21, 0x1B, 0))
        images = gbr_to_png.decode_gbr(gbr_v3(first, second))
        self.assertEqual(len(images), 2)
        self.assertEqual([image.margin for image in images], [0x12, 0x21])
        self.assertEqual(list(images[0].pixels[:4]), [255, 170, 85, 0])
        self.assertEqual(list(images[1].pixels[:4]), [127, 170, 212, 255])
        self.assertEqual(list(gbr_to_png.decode_gbr(gbr_v3(first, second), 0xE4)[1].pixels[:4]),
                         [255, 212, 170, 127])

        width, height, pixels = gbr_to_png.merge_images(images)
        self.assertEqual((width, height), (8, 64))
        self.assertEqual(pixels[0], 255)  # First block's before margin.
        self.assertEqual(pixels[8 * 8 + 3], 0)
        self.assertEqual(pixels[48 * 8], 127)
        self.assertEqual(pixels[63 * 8], 255)  # Last block's after margin.

    def test_cli_single_ignores_margin_and_merge_uses_file_palettes(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.gbr"
            second = Path(directory) / "second.gbr"
            first.write_bytes(gbr(control=(1, 0x12, 0xE4, 0x7F)))
            second.write_bytes(gbr(control=(1, 0x21, 0x1B, 0)))

            subprocess.run([sys.executable, str(SCRIPT), str(first)], check=True, capture_output=True)
            with Image.open(first.with_suffix(".png")) as image:
                self.assertEqual(image.size, (8, 8))
                self.assertEqual(image.getpixel((3, 0)), 0)

            output = Path(directory) / "merged.png"
            subprocess.run([sys.executable, str(SCRIPT), str(first), str(second), "-o", str(output)],
                           check=True, capture_output=True)
            with Image.open(output) as image:
                self.assertEqual(image.size, (8, 64))
                self.assertEqual(image.getpixel((3, 0)), 255)
                self.assertEqual(image.getpixel((3, 8)), 0)
                self.assertEqual(image.getpixel((3, 16)), 255)
                self.assertEqual(image.getpixel((0, 48)), 127)
                self.assertEqual(image.getpixel((3, 56)), 255)

            subprocess.run([sys.executable, str(SCRIPT), str(first), str(second), "--palette", "0xE4",
                            "-o", str(output)], check=True, capture_output=True)
            with Image.open(output) as image:
                self.assertEqual(image.getpixel((0, 48)), 255)
                self.assertEqual(image.getpixel((3, 48)), 127)

    def test_cli_merges_multiple_v3_files_and_final_margin(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.gbr"
            second = Path(directory) / "second.gbr"
            first.write_bytes(gbr_v3(gbr(control=(2, 0x10, 0xE4, 0x7F)),
                                         gbr(control=(3, 0x00, 0x1B, 0x00))))
            second.write_bytes(gbr_v3(gbr(control=(0, 0x01, 0xE4, 0x7F))))
            output = Path(directory) / "merged.png"
            subprocess.run([sys.executable, str(SCRIPT), str(first), str(second), "-o", str(output)],
                           check=True, capture_output=True)
            with Image.open(output) as image:
                self.assertEqual(image.size, (8, 40))
                self.assertEqual(image.getpixel((3, 0)), 255)
                self.assertEqual(image.getpixel((3, 8)), 0)
                self.assertEqual(image.getpixel((0, 16)), 127)
                self.assertEqual(image.getpixel((3, 24)), 0)
                self.assertEqual(image.getpixel((3, 39)), 255)

    def test_rejects_invalid_header_size_and_mismatched_width(self):
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr()[:11])
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr() + b"\x00")
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr_v3())
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr_v3(gbr())[:-1])
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr_v3(gbr()) + b"\x00")
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr_v3(gbr()).replace(b"\x02\x08", b"\x03\x08", 1))
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(b"KGBR\x03\x02" + gbr()[4:])
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.merge_images(gbr_to_png.decode_gbr(gbr()) +
                                    gbr_to_png.decode_gbr(gbr(width=16)))


if __name__ == "__main__":
    unittest.main()
