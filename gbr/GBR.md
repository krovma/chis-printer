# GBR binary format

M1 saves completed print jobs as `DCIM/00001.gbr`, `DCIM/00002.gbr`, and so on. Names remain numeric beyond five digits. The file FourCC is ASCII `KGBR`.

## Version 3

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 4 | FourCC | ASCII `KGBR` |
| 4 | 1 | File version | `3` |
| 5 | 1 | Block count | Number of blocks in this file |
| 6 | Variable | Blocks | Exactly `Block count` consecutive blocks |

Each block begins with the following eight-byte header, which is the v2 header without its FourCC. Offsets below are relative to the start of the block.

| Offset | Size | Field | Encoding |
| --- | ---: | --- | --- |
| 0 | 1 | Block version | `2`; independent of the file version |
| 1 | 1 | Width | Pixels, currently `160` |
| 2 | 2 | Height | Pixels, little-endian |
| 4 | 1 | Sheets | Original PRINT (`0x02`) payload byte 1 |
| 5 | 1 | Margins | Original PRINT payload byte 2; high nibble feeds before, low nibble feeds after |
| 6 | 1 | Palette | Original PRINT payload byte 3 |
| 7 | 1 | Exposure | Original PRINT payload byte 4 |
| 8 | `width * height / 4` | Pixels | Raw, uncompressed Game Boy 2bpp tile bytes |

The block version remains `2` so future block formats can be distinguished from the file version. Width and height are positive multiples of 8. Tiles are 8 x 8 pixels, ordered left to right across each 8-pixel-high row, then top to bottom. Each tile uses 16 bytes: one low bit-plane byte followed by one high bit-plane byte for every pixel row. Bit 7 is the leftmost pixel; its index is `low_bit | (high_bit << 1)`. No padding, checksum, or trailing data is stored.

Sheets is recorded as received, but does not multiply the number of stored or rendered blocks. M2 sends each block to the physical printer with Sheets set to `1`, regardless of its recorded value, to avoid printing duplicate copies. Its other PRINT bytes are preserved. GB Camera typically sends margins `0x13`, palette `0xE4`, and exposure `0x40`.

M1 starts a new v3 file for the first block. When another DATA group starts within `m1.auto_cut_time_limit` after a print-complete reply, M1 appends its block to the preceding file and updates the block count. The default limit is one second; zero disables merging. If the preceding file is unavailable, a new file is created.

## Earlier versions and PNG conversion

Standalone v2 files contain `KGBR`, version `2`, and one block header without a separate block count. Standalone v1 files contain `KGBR`, version `1`, width, height, and pixels; readers use palette `0xE4`, exposure `0x7F`, and no margins.

`gbr-to-png.py` reads v1, v2, and v3 files. It treats v3 blocks like consecutive v2 inputs and joins all blocks from all input files in command-line order. It uses each block's palette unless `--palette` overrides every block. Exposure `0x00` gives darkest shade `0x7F`, while `0x7F` gives `0x00`; other shades are linearly spaced from white (`0xFF`) to that darkest shade. With one block total, margins are ignored. With multiple blocks, every block's before and after nibble adds that many blank 8-pixel rows, including before the first and after the last block.
