#!/usr/bin/env python3
"""Generate the P4's antialiased 24px Chinese face (OFL; GB2312 + UI symbols).

Usage: python scripts/generate_p4_font.py path/to/NotoSansSC.ttf
Source: https://github.com/google/fonts/tree/main/ofl/notosanssc
License: docs/licenses/NotoSansSC-OFL.txt
Requires freetype-py. The binary is committed so normal builds need no converter.
"""
import sys
from pathlib import Path
from generate_vlw_fonts import generate_vlw, vlw_to_header
root = Path(__file__).resolve().parents[1]
chars = set(range(32, 127)) | {0xb0, 0xb7, 0x2014, 0x2013, 0x2026, 0x2248}
for high in range(0xa1, 0xf8):
    for low in range(0xa1, 0xff):
        try: chars.add(ord(bytes([high, low]).decode('gb2312')))
        except UnicodeDecodeError: pass
for ch in (root / 'src/display_p4.cpp').read_text():
    if ord(ch)>127: chars.add(ord(ch))
for size in (20, 24):
    data = generate_vlw(sys.argv[1], size, sorted(chars), weight=400)
    (root / f'assets/p4/p4_sans_{size}.vlw').write_bytes(data)
    print(f'{size}px: {len(data):,} bytes; {len(chars)} requested glyphs')

data = generate_vlw(sys.argv[1], 32, sorted(set(range(32,127)) | {ord(c) for c in '小时分钟—'}), weight=400)
(root / 'include/fonts/p4_duration.h').write_text(vlw_to_header('p4_duration', data))
