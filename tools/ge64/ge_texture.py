#!/usr/bin/env python3
"""Decode a GoldenEye texture (via the getex host tool built from the GE decomp) into RGBA8888 pixels."""
import os, re, subprocess, struct

HERE = os.path.dirname(os.path.abspath(__file__))
GETEX = os.path.join(HERE, 'getex', 'getex')
# GE_DECOMP: checkout of the GoldenEye 007 decomp with its assets extracted (setup.sh sets this)
GE_DECOMP = os.environ.get('GE_DECOMP', os.path.join(HERE, '007'))
SPLIT = os.path.join(GE_DECOMP, 'assets', 'images', 'split')

# TEXFORMAT ids from the GE decomp image.h, derived from (gbiformat, depth, lutmode)
def _row_bytes(fmt_bits, width):
    # rows are padded to 8 bytes (texAlignIndices / texSwapAltRowBytes)
    return ((width * fmt_bits + 63) // 64) * 8

def _unswap(data, width, height, bits):
    """Undo texSwapAltRowBytes: odd rows have 32-bit words swapped (pairs, or quads for 32bpp)."""
    rb = _row_bytes(bits, width)
    out = bytearray(data)
    for y in range(1, height, 2):
        row = out[y * rb:(y + 1) * rb]
        words = [row[i:i + 4] for i in range(0, len(row), 4)]
        step = 4 if bits == 32 else 2
        for x in range(0, len(words) - step + 1, step):
            if step == 4:
                words[x], words[x + 2] = words[x + 2], words[x]
                words[x + 1], words[x + 3] = words[x + 3], words[x + 1]
            else:
                words[x], words[x + 1] = words[x + 1], words[x]
        out[y * rb:(y + 1) * rb] = b''.join(words)
    return bytes(out)

def _rgba5551(v):
    r = (v >> 11) & 31; g = (v >> 6) & 31; b = (v >> 1) & 31; a = v & 1
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if a else 0)

def decode(image_name, tmpdir='/tmp'):
    """image_name: 'IMAGE_2157' or 'IMAGE_BARRELSPECULAR'. Returns (width, height, [rgba tuples])."""
    name = image_name.replace('IMAGE_', '')
    src = os.path.join(SPLIT, name + '.bin')
    raw = os.path.join(tmpdir, 'getex_%s.raw' % name)
    info = subprocess.run([GETEX, src, raw], check=True, capture_output=True, text=True).stdout
    f = dict(kv.split('=') for kv in info.split())
    w, h = int(f['width']), int(f['height'])
    gbi, depth, lut, ncol = int(f['gbiformat']), int(f['depth']), int(f['lutmode']), int(f['numcolours'])
    total = int(f['bytes'])
    data = open(raw, 'rb').read()
    bits = {0: 4, 1: 8, 2: 16, 3: 32}[depth]
    explicit, lods = (int(x) for x in f['lods'].split('/'))
    # texInflate* only TMEM-swaps odd rows when mipmaps are involved
    swapped = (explicit == 1 and lods > 0) or (explicit == 0 and lods >= 1)
    unswap = _unswap if swapped else (lambda data, w, h, bits: data)
    pix = []
    if gbi == 2:  # CI with palette after the image chain
        pal_bytes = ((ncol * 2 + 7) // 8) * 8
        pal_off = total - pal_bytes
        pal = [struct.unpack('>H', data[pal_off + 2 * i:pal_off + 2 * i + 2])[0] for i in range(ncol)]
        img = unswap(data, w, h, bits)
        rb = _row_bytes(bits, w)
        for y in range(h):
            for x in range(w):
                if bits == 8:
                    idx = img[y * rb + x]
                else:
                    b = img[y * rb + x // 2]
                    idx = (b >> 4) if x % 2 == 0 else (b & 15)
                v = pal[idx] if idx < len(pal) else 0
                if lut == 3:  # IA16 palette
                    i8, a8 = v >> 8, v & 0xFF
                    pix.append((i8, i8, i8, a8))
                else:
                    pix.append(_rgba5551(v))
    else:
        img = unswap(data, w, h, bits)
        rb = _row_bytes(bits, w)
        for y in range(h):
            row = img[y * rb:(y + 1) * rb]
            for x in range(w):
                if gbi == 0 and bits == 32:
                    # written as host-endian u32 by the decompressor (x86: bytes are A,B,G,R)
                    pix.append((row[4 * x + 3], row[4 * x + 2], row[4 * x + 1], row[4 * x]))
                elif gbi == 0 and bits == 16:
                    pix.append(_rgba5551(struct.unpack('>H', row[2 * x:2 * x + 2])[0]))
                elif gbi == 3 and bits == 16:
                    pix.append((row[2 * x], row[2 * x], row[2 * x], row[2 * x + 1]))
                elif gbi == 3 and bits == 8:
                    i4, a4 = row[x] >> 4, row[x] & 15
                    pix.append((i4 * 17, i4 * 17, i4 * 17, a4 * 17))
                elif gbi == 3 and bits == 4:
                    b = row[x // 2]; v = (b >> 4) if x % 2 == 0 else (b & 15)
                    i3, a1 = v >> 1, v & 1
                    pix.append((i3 * 36, i3 * 36, i3 * 36, 255 if a1 else 0))
                elif gbi == 4 and bits == 8:
                    pix.append((row[x], row[x], row[x], 255))
                elif gbi == 4 and bits == 4:
                    b = row[x // 2]; v = (b >> 4) if x % 2 == 0 else (b & 15)
                    pix.append((v * 17, v * 17, v * 17, 255))
                else:
                    raise ValueError('unhandled format gbi=%d bits=%d' % (gbi, bits))
    return w, h, pix, f
