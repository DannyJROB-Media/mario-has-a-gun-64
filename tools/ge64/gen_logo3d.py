#!/usr/bin/env python3
"""Generate the 3D "MARIO HAS A GUN 64" title logo as an F3D display list (extruded lettering with
baked vertex colours), in the space of the original SM64 logo (camera at z=3200, letters ~2700 wide).

usage: gen_logo3d.py <output .inc.c> [preview.png]
"""
import math, sys
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/opentype/cantarell/Cantarell-VF.otf'
DEPTH = 220
PALETTE = [(230, 30, 30), (40, 170, 60), (250, 210, 20), (40, 90, 230)]

def font(size):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_name('Extra Bold')
    except Exception:
        pass
    return f

def glyph_mask(ch, px):
    f = font(px)
    l, t, r, b = f.getbbox(ch)
    w, h = r - l + 2, px + 4
    im = Image.new('L', (w, h), 0)
    ImageDraw.Draw(im).text((1 - l, 1), ch, font=f, fill=255)
    return im.point(lambda v: 255 if v > 110 else 0), f.getlength(ch)

def rects(mask):
    """Greedy merge of horizontal runs into rectangles (x0, y0, x1, y1) in cell units."""
    w, h = mask.size
    px = mask.load()
    rows = []
    for y in range(h):
        runs, x = [], 0
        while x < w:
            if px[x, y]:
                s = x
                while x < w and px[x, y]:
                    x += 1
                runs.append((s, x))
            else:
                x += 1
        rows.append(runs)
    out, open_ = [], {}
    for y, runs in enumerate(rows + [[]]):
        nxt = {}
        for run in runs:
            if run in open_:
                nxt[run] = open_.pop(run)
            else:
                nxt[run] = y
        for run, y0 in open_.items():
            out.append((run[0], y0, run[1], y))
        open_ = nxt
    return out

def edges(mask):
    """Boundary edges as merged segments: (kind, fixed, a0, a1) with kind in top/bottom/left/right."""
    w, h = mask.size
    px = mask.load()
    filled = lambda x, y: 0 <= x < w and 0 <= y < h and px[x, y] > 0
    segs = []
    for kind, dx, dy in (('top', 0, -1), ('bottom', 0, 1)):
        for y in range(h):
            x = 0
            while x < w:
                if filled(x, y) and not filled(x + dx, y + dy):
                    s = x
                    while x < w and filled(x, y) and not filled(x + dx, y + dy):
                        x += 1
                    segs.append((kind, y if kind == 'top' else y + 1, s, x))
                else:
                    x += 1
    for kind, dx, dy in (('left', -1, 0), ('right', 1, 0)):
        for x in range(w):
            y = 0
            while y < h:
                if filled(x, y) and not filled(x + dx, y + dy):
                    s = y
                    while y < h and filled(x, y) and not filled(x + dx, y + dy):
                        y += 1
                    segs.append((kind, x if kind == 'left' else x + 1, s, y))
                else:
                    y += 1
    return segs

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

quads = []  # list of (4 points, 4 colours)

def add_letter(ch, colour, px, cell, ox, oy, tilt_deg, lift):
    mask, adv = glyph_mask(ch, px)
    w, h = mask.size
    ca, sa = math.cos(math.radians(tilt_deg)), math.sin(math.radians(tilt_deg))
    cx, cy = w * cell / 2, h * cell / 2

    def P(gx, gy, z):
        x = gx * cell - cx
        y = -(gy * cell - cy)
        return (ox + cx + x * ca - y * sa, oy + lift + y * ca + x * sa, z)

    top_c, bot_c = shade(colour, 1.15), shade(colour, 0.75)
    for x0, y0, x1, y1 in rects(mask):
        def grad(gy):
            t = gy / h
            return tuple(int(top_c[i] * (1 - t) + bot_c[i] * t) for i in range(3))
        quads.append(([P(x0, y1, DEPTH / 2), P(x1, y1, DEPTH / 2), P(x1, y0, DEPTH / 2), P(x0, y0, DEPTH / 2)],
                      [grad(y1), grad(y1), grad(y0), grad(y0)]))
        back = shade(colour, 0.3)
        quads.append(([P(x0, y0, -DEPTH / 2), P(x1, y0, -DEPTH / 2), P(x1, y1, -DEPTH / 2), P(x0, y1, -DEPTH / 2)],
                      [back] * 4))
    side_k = {'top': 0.95, 'bottom': 0.35, 'left': 0.55, 'right': 0.6}
    for kind, fixed, a0, a1 in edges(mask):
        c = shade(colour, side_k[kind])
        f, b = DEPTH / 2, -DEPTH / 2
        if kind in ('top', 'bottom'):
            pts = [P(a0, fixed, f), P(a1, fixed, f), P(a1, fixed, b), P(a0, fixed, b)]
        else:
            pts = [P(fixed, a0, f), P(fixed, a1, f), P(fixed, a1, b), P(fixed, a0, b)]
        quads.append((pts, [c] * 4))
    return adv * cell

def add_line(text, px, cell, y, palette_start, x_center=0, x_extra=0, colours=None):
    # measure, then lay out centred
    gap = 0.14 * px * cell
    total = sum(font(px).getlength(ch) for ch in text) * cell + gap * (len(text) - 1)
    x = x_center - total / 2 + x_extra
    k = palette_start
    for i, ch in enumerate(text):
        if ch == ' ':
            x += font(px).getlength(ch) * cell + gap
            continue
        colour = colours[i] if colours else PALETTE[k % len(PALETTE)]
        k += 1
        tilt = (-6 if i % 2 else 5) * min(1.0, px / 40.0) * 0.6
        lift = (40 if i % 2 else -10) * cell / 26.0
        x += add_letter(ch, colour, px, cell, x, y, tilt, lift) + gap

# MARIO big on top, "HAS A GUN" below with a red "64" tucked at the right, like the original 64
add_line('MARIO', 40, 17.0, 250, 0, x_extra=-40)
add_line('HAS A GUN', 26, 12.5, -330, 1, x_extra=-330)
add_line('64', 34, 15.0, -330, 0, x_extra=1200, colours=[(220, 30, 30), (220, 30, 30)])

# emit: 4 quads (16 vertices) per gsSPVertex load
vtx_lines, gfx_lines = [], []
for qi, (pts, cols) in enumerate(quads):
    uvs = [(0, 0)] * 4
    for p, c in zip(pts, cols):
        vtx_lines.append('    {{{ %6d, %6d, %6d }, 0, { 0, 0 }, { 0x%02X, 0x%02X, 0x%02X, 0xFF }}},'
                         % (round(p[0]), round(p[1]), round(p[2]), c[0], c[1], c[2]))
for i in range(0, len(quads), 4):
    n = min(4, len(quads) - i)
    gfx_lines.append('    gsSPVertex(ge64_logo_vtx + %d, %d, 0),' % (i * 4, n * 4))
    for q in range(n):
        b = q * 4
        gfx_lines.append('    gsSP1Triangle(%d, %d, %d, 0x0),' % (b, b + 1, b + 2))
        gfx_lines.append('    gsSP1Triangle(%d, %d, %d, 0x0),' % (b, b + 2, b + 3))

out = ['// Generated by tools/ge64/gen_logo3d.py: "MARIO HAS A GUN 64" title logo (%d quads)' % len(quads),
       'static const Vtx ge64_logo_vtx[] = {'] + vtx_lines + ['};', '',
       'Gfx intro_seg7_dl_ge64_logo[] = {',
       '    gsDPPipeSync(),',
       '    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),',
       '    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),',
       '    gsSPClearGeometryMode(G_LIGHTING | G_CULL_BACK),',
       '    gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER),'] + gfx_lines + [
       '    gsSPSetGeometryMode(G_LIGHTING | G_CULL_BACK),',
       '    gsSPEndDisplayList(),', '};']
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
print('quads', len(quads), 'vertices', len(quads) * 4)

if len(sys.argv) > 2:
    # quick perspective preview from the title camera (z=3200, fov 45, 4:3)
    W, H = 640, 480
    img = Image.new('RGB', (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    f = 1 / math.tan(math.radians(22.5))
    tris = []
    for pts, cols in quads:
        sc = []
        for x, y, z in pts:
            zz = 3200 - z
            sc.append((W / 2 + x / zz * f * H / 2, H / 2 - y / zz * f * H / 2))
        tris.append((sum(3200 - p[2] for p in pts), sc, cols[0]))
    for _, sc, c in sorted(tris, key=lambda t: -t[0]):
        d.polygon(sc, fill=c)
    img.save(sys.argv[2])
