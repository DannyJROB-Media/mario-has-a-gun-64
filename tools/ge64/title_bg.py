#!/usr/bin/env python3
"""Redraw the tiled title-screen backdrop (behind Mario's head) to read "MARIO HAS A GUN 64",
in the style of the original: dark blue mottled tile, light-blue lettering, red "64".
Writes the four 80x20 strips of the title tile (the game-over tiles are untouched)."""
import os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = '/usr/share/fonts/opentype/cantarell/Cantarell-VF.otf'
root = sys.argv[1]
out_dir = os.path.join(root, 'textures/title_screen_bg')
STRIPS = ['title_screen_bg.001C0.rgba16.png', 'title_screen_bg.00E40.rgba16.png',
          'title_screen_bg.01AC0.rgba16.png', 'title_screen_bg.02740.rgba16.png']

def font(size):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_name('Extra Bold')
    except Exception:
        pass
    return f

S = 4  # supersample
W = H = 80 * S
random.seed(64)
bg = Image.new('RGB', (W, H))
px = bg.load()
for y in range(H):
    for x in range(W):
        # dark blue with a soft vignette and grain, like the original tile
        d = ((x - W / 2) ** 2 + (y - H / 2) ** 2) ** 0.5 / (W / 2)
        base = max(0.0, 1.0 - 0.55 * d)
        n = random.uniform(-0.18, 0.18)
        px[x, y] = (int(18 * base + 10 * n), int(14 * base + 8 * n), int(70 * base + 30 * n + 10))
bg = bg.filter(ImageFilter.GaussianBlur(1.2))

text = Image.new('RGBA', (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(text)
def draw_centered(s, f, y, fill, x_off=0, tilt=0):
    w = d.textlength(s, font=f)
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text(((W - w) / 2 + x_off, y), s, font=f, fill=fill)
    if tilt:
        layer = layer.rotate(tilt, resample=Image.BICUBIC, center=(W / 2, y + f.size / 2))
    text.alpha_composite(layer)

blue = (90, 150, 235, 255)
draw_centered('MARIO', font(21 * S), 12 * S, blue, x_off=0, tilt=4)
draw_centered('HAS A GUN', font(11 * S), 38 * S, blue, x_off=-3 * S, tilt=-3)
draw_centered('64', font(15 * S), 51 * S, (200, 40, 40, 255), x_off=22 * S, tilt=-10)
# mottle the letters a little so they sit in the texture like the original
mask = text.split()[3].filter(ImageFilter.GaussianBlur(0.8))
grain = Image.effect_noise((W, H), 40).convert('L')
lit = Image.merge('RGB', [c.point(lambda v: v) for c in text.convert('RGB').split()])
lit = Image.composite(lit, Image.eval(lit, lambda v: int(v * 0.8)), grain.point(lambda v: 255 if v > 120 else 0))
img = Image.composite(lit, bg, mask).resize((80, 80), Image.LANCZOS)

for i, name in enumerate(STRIPS):
    img.crop((0, 20 * i, 80, 20 * (i + 1))).convert('RGBA').save(os.path.join(out_dir, name))
if len(sys.argv) > 2:
    img.resize((320, 320), Image.NEAREST).save(sys.argv[2])
print('wrote', len(STRIPS), 'strips')
