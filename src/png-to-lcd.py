#!/usr/bin/env python3
# Converts a PNG (or any image Pillow can open) into a raw XBM bitmap
# file g510_lcd_stats.c can load directly via g15r_drawXBM(). Only does
# the ONE-TIME conversion step -- all runtime drawing stays in C, no
# image library needed there.
import sys
import re
from PIL import Image

if len(sys.argv) != 5:
    print("usage: png-to-lcd.py <input.png> <output.bin> <max_width> <max_height>")
    sys.exit(1)

infile, outfile, max_width, max_height = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])

img = Image.open(infile).convert("L")  # grayscale first
w, h = img.size
# fit within max_width x max_height (never upscale) -- a tall/narrow source
# image constrained only by width could still end up taller than the LCD
ratio = min(max_width / w, max_height / h, 1.0)
if ratio < 1.0:
    img = img.resize((max(1, int(w * ratio)), max(1, int(h * ratio))))
    w, h = img.size

img = img.convert("1")  # dithered 1-bit conversion

# Pillow's XBM save writes a C-style file (#define + byte array) --
# extract just the raw bytes from it.
img.save("/tmp/_png2lcd_tmp.xbm", "XBM")
with open("/tmp/_png2lcd_tmp.xbm") as f:
    xbm_src = f.read()

hex_bytes = re.findall(r"0x([0-9A-Fa-f]{2})", xbm_src)
raw = bytes(int(h, 16) for h in hex_bytes)

with open(outfile, "wb") as f:
    f.write(raw)

print(f"{w} {h}")
