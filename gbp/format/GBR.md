# GBR v2 binary format

M1 saves each completed image as `DCIM/00001.gbr`, `DCIM/00002.gbr`, and so on. The `.gbr` extension identifies a GBR file with the `KGBR` FourCC.

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | FourCC | ASCII `KGBR` |
| 4 | 1 | Version | Unsigned 8-bit integer, currently `2` |
| 5 | 1 | Width | Unsigned 8-bit pixels, currently `160` |
| 6 | 2 | Height | Unsigned 16-bit pixels, little-endian |
| 8 | 1 | Sheets | PRINT (`0x02`) payload byte 1, number of sheets (`0` means line feed only) |
| 9 | 1 | Margins | PRINT payload byte 2; high nibble feeds before, low nibble feeds after |
| 10 | 1 | Palette | PRINT payload byte 3, typically `0xE4` |
| 11 | 1 | Exposure | PRINT payload byte 4; low 7 bits control print darkness |
| 12 | `width * height / 4` | Pixels | Raw, uncompressed Game Boy 2bpp tile bytes |

V2 images have a height that is a positive multiple of 8. Tiles are 8 x 8 pixels, ordered left to right across each 8-pixel-high row, then top to bottom. Each tile uses 16 bytes: for each pixel row, one low bit-plane byte followed by one high bit-plane byte. Within each byte, bit 7 is the leftmost pixel. Pixel index is `low_bit | (high_bit << 1)`. The four PRINT bytes are copied without modification, including the high bit of the exposure byte. No padding, checksum, or trailing data is stored. GB Camera typically sends margins `0x13` and exposure `0x40`.

For example, a 160 x 16 image with PRINT payload `01 13 E4 40` has a header of `4B 47 42 52 02 A0 10 00 01 13 E4 40` followed by 640 tile bytes. Files are numbered from `00001.gbr`; the initial number is found by scanning existing numbered `.gbr` files on the card. Names remain numeric beyond five digits.

`gbr-to-png.py` also reads legacy v1 files (8-byte header), using palette `0xE4`, exposure `0x7F`, and no margins. For v2, it reads the file palette unless `--palette` overrides it. Exposure `0x00` gives darkest shade `0x7F`, while `0x7F` gives `0x00`; the other shades are linearly spaced between white (`0xFF`) and that darkest shade. For a single input, margins are ignored. With multiple inputs, each margin nibble adds that many blank 8-pixel rows before or after its image, and images are joined in command-line order.
