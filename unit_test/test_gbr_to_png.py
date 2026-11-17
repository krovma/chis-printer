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


class GbrToPngTests(unittest.TestCase):
    def test_v2_palette_exposure_and_v1_compatibility(self):
        light_print = gbr(control=(1, 0x13, 0xE4, 0))
        dark_print = gbr(control=(1, 0x13, 0xE4, 0x7F))
        self.assertEqual(list(gbr_to_png.decode_gbr(light_print).pixels[:4]), [255, 212, 170, 127])
        self.assertEqual(list(gbr_to_png.decode_gbr(dark_print).pixels[:4]), [255, 170, 85, 0])
        self.assertEqual(list(gbr_to_png.decode_gbr(gbr(version=1)).pixels[:4]), [255, 170, 85, 0])
        self.assertEqual(list(gbr_to_png.decode_gbr(dark_print, 0x1B).pixels[:4]), [0, 85, 170, 255])
        self.assertEqual(gbr_to_png.decode_gbr(gbr(control=(1, 0x13, 0xE4, 0xFF))).pixels,
                         gbr_to_png.decode_gbr(dark_print).pixels)

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

    def test_rejects_invalid_header_size_and_mismatched_width(self):
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr()[:11])
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr() + b"\x00")
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.decode_gbr(gbr(version=3))
        with self.assertRaises(gbr_to_png.GBRFormatError):
            gbr_to_png.merge_images([gbr_to_png.decode_gbr(gbr()),
                                     gbr_to_png.decode_gbr(gbr(width=16))])


if __name__ == "__main__":
    unittest.main()
