#!/usr/bin/env python3
"""
bmp16_convert.py - Convert 24-bit BMP to 16-bit ARGB1555 or RGB565 BMP.

Usage:
    python bmp16_convert.py input.bmp output.bmp --format argb1555
    python bmp16_convert.py input.bmp output.bmp --format rgb565

Requires: Pillow  (pip install Pillow)
"""

import argparse
import struct
from PIL import Image


def rgb_to_argb1555(r, g, b, a=255):
    """Convert 8-bit RGBA to 16-bit ARGB1555."""
    a1 = 1 if a >= 128 else 0
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return (a1 << 15) | (r5 << 10) | (g5 << 5) | b5


def rgb_to_rgb565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def convert(input_path, output_path, fmt):
    img = Image.open(input_path).convert("RGBA")
    width, height = img.size
    pixels = img.load()

    # Each row is 2 bytes per pixel, padded to 4-byte boundary
    row_bytes = width * 2
    row_padding = (4 - (row_bytes % 4)) % 4
    padded_row = row_bytes + row_padding
    pixel_data_size = padded_row * height

    # --- Build BMP with BITMAPINFOHEADER (40 bytes) + 3 color masks (12 bytes) ---
    # File header: 14 bytes
    # DIB header:  40 bytes (BITMAPINFOHEADER)
    # Color masks: 12 bytes (3 x uint32)
    header_size = 14 + 40 + 12
    file_size = header_size + pixel_data_size

    # Bitfield masks
    if fmt == "argb1555":
        r_mask = 0x7C00
        g_mask = 0x03E0
        b_mask = 0x001F
    else:  # rgb565
        r_mask = 0xF800
        g_mask = 0x07E0
        b_mask = 0x001F

    # -- BITMAPFILEHEADER (14 bytes) --
    bfh = struct.pack('<2sIHHI',
        b'BM',
        file_size,
        0,              # reserved
        0,              # reserved
        header_size,    # offset to pixel data
    )

    # -- BITMAPINFOHEADER (40 bytes) --
    bih = struct.pack('<IiiHHIIiiII',
        40,             # header size
        width,
        height,         # positive = bottom-up row order
        1,              # planes
        16,             # bits per pixel
        3,              # compression = BI_BITFIELDS
        pixel_data_size,
        2835, 2835,     # pixels per meter (~72 dpi)
        0, 0,           # colors used / important
    )

    # -- Color masks (12 bytes, appended after BITMAPINFOHEADER) --
    masks = struct.pack('<III', r_mask, g_mask, b_mask)

    # -- Pixel data (bottom-up: last row first) --
    rows = []
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if fmt == "argb1555":
                val = rgb_to_argb1555(r, g, b, a)
            else:
                val = rgb_to_rgb565(r, g, b)
            row += struct.pack('<H', val)
        row += b'\x00' * row_padding
        rows.append(bytes(row))

    with open(output_path, 'wb') as f:
        f.write(bfh)
        f.write(bih)
        f.write(masks)
        for row in rows:
            f.write(row)

    print(f"Saved {fmt.upper()} 16-bit BMP: {output_path}  ({width}x{height})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert 24-bit BMP to 16-bit BMP")
    parser.add_argument("input", help="Input image (BMP, PNG, etc.)")
    parser.add_argument("output", help="Output 16-bit BMP path")
    parser.add_argument("--format", choices=["argb1555", "rgb565"], default="rgb565",
                        help="Target pixel format (default: rgb565)")
    args = parser.parse_args()
    convert(args.input, args.output, args.format)
