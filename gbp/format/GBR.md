# GBR v1 binary format

M1 saves each completed image as `DCIM/00001.gbr`, `DCIM/00002.gbr`, and so on. The `.gbr` extension identifies a GBR file with the `KGBR` FourCC.

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | FourCC | ASCII `KGBR` |
| 4 | 1 | Version | Unsigned 8-bit integer, currently `1` |
| 5 | 1 | Width | Unsigned 8-bit pixels, currently `160` |
| 6 | 2 | Height | Unsigned 16-bit pixels, little-endian |
| 8 | `width * height / 4` | Pixels | Raw, uncompressed Game Boy 2bpp tile bytes |

V1 images have a height that is a positive multiple of 8. Tiles are 8 x 8 pixels, ordered left to right across each 8-pixel-high row, then top to bottom. Each tile uses 16 bytes: for each pixel row, one low bit-plane byte followed by one high bit-plane byte. Within each byte, bit 7 is the leftmost pixel. Pixel index is `low_bit | (high_bit << 1)`. No palette, padding, checksum, or trailing data is stored.

For example, a 160 x 16 image has a header of `4B 47 42 52 01 A0 10 00` followed by 640 tile bytes. Files are numbered from `00001.gbr`; the initial number is found by scanning existing numbered `.gbr` files on the card. Names remain numeric beyond five digits.
