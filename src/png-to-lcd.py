#!/usr/bin/env python3
# Converts a PNG (or any image Pillow can open) into a raw XBM bitmap
# file g510_lcd_stats.c can load directly via g15r_drawXBM(). Only does
# the ONE-TIME conversion step -- all runtime drawing stays in C, no
# image library needed there.
import sys
import re
import tempfile
import os
from PIL import Image, ImageOps

if len(sys.argv) != 5:
    print("usage: png-to-lcd.py <input.png> <output.bin> <max_width> <max_height>")
    sys.exit(1)

infile, outfile, max_width, max_height = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])

img = Image.open(infile)

# Real bug found via direct testing: a transparent PNG (the normal case
# for a real logo file -- confirmed by reproducing with a synthetic
# transparent-background test image) rendered as a SOLID BLACK BOX,
# completely swallowing the actual logo shape. PIL's own RGBA->L
# conversion silently drops the alpha channel and keeps whatever RGB
# value a transparent pixel happens to carry -- for freshly-created
# transparent pixels that's (0,0,0), i.e. black, not the white/
# background look you'd actually want. Compositing onto a real white
# background FIRST (only when there's an alpha channel to composite)
# makes transparent areas become background, same as "LIVE" would look
# on a real poster of this logo.
if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
    img = img.convert("RGBA")
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img = Image.alpha_composite(bg, img)

img = img.convert("L")  # grayscale
w, h = img.size
# fit within max_width x max_height (never upscale) -- a tall/narrow source
# image constrained only by width could still end up taller than the LCD
ratio = min(max_width / w, max_height / h, 1.0)
if ratio < 1.0:
    img = img.resize((max(1, int(w * ratio)), max(1, int(h * ratio))))
    w, h = img.size

# Real quality issue found via direct A/B comparison on a simple
# two-tone test shape: the default Floyd-Steinberg dither (img.convert
# ("1") with no args) is the right call for a photo, but for a flat-
# color logo -- the only kind of image this project's own docs now
# tell you to expect, e.g. "an AMD logo" -- it turns clean mid-tone
# fills into visible speckle noise instead of a crisp edge. Stretching
# the tonal range first (autocontrast) then using a plain threshold
# (no error-diffusion) gives simple logos sharp, clean edges; it's a
# worse choice for an actual photograph, but this project's own README
# now explicitly scopes imports to simple high-contrast graphics, not
# photos, so this is the right default for what's actually supported.
img = ImageOps.autocontrast(img)
img = img.point(lambda p: 255 if p > 128 else 0).convert("1")

# Pillow's XBM save writes a C-style file (#define + byte array) --
# extract just the raw bytes from it. A unique temp filename (not a
# fixed path) avoids two conversions racing each other -- a real risk
# here: the GUI runs this both for a fresh import AND, on a background
# thread, every time you drag an image's resize handle, so two could
# genuinely overlap.
with tempfile.NamedTemporaryFile(suffix=".xbm", delete=False) as tmp:
    tmp_path = tmp.name
try:
    img.save(tmp_path, "XBM")
    with open(tmp_path) as f:
        xbm_src = f.read()
finally:
    os.unlink(tmp_path)

hex_bytes = re.findall(r"0x([0-9A-Fa-f]{2})", xbm_src)
# Real bug found via direct testing: Pillow's XBM writer encodes bit=1
# for a WHITE pixel (confirmed by comparing the raw bytes against a
# hand-computed expectation, byte-for-byte identical) -- but
# g15r_drawXBM (libg15render) draws bit=1 as BLACK. The two disagree,
# so every imported image rendered as a photo negative: confirmed by
# rendering a plain black circle on white through the real C binary
# and getting a solid dark box with a light HOLE where the circle
# should be. Inverting every byte here makes our .bin format match
# what g15r_drawXBM actually expects.
raw = bytes((~int(h, 16)) & 0xFF for h in hex_bytes)

with open(outfile, "wb") as f:
    f.write(raw)

print(f"{w} {h}")
