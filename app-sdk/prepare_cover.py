#!/usr/bin/env python3
"""Convert album art to the music player's 64x64 little-endian RGB565 format.
Requires Pillow: python3 -m pip install Pillow
"""
import argparse
import struct
from pathlib import Path
from PIL import Image, ImageOps
p = argparse.ArgumentParser()
p.add_argument('image', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
im = ImageOps.fit(Image.open(a.image).convert('RGB'), (64, 64))
a.output.write_bytes(b''.join(struct.pack('<H', ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)) for r, g, b in im.getdata()))
print(a.output)
