#!/usr/bin/env python3
"""Put Mario in a black suit with a white shirt front, keeping his red cap.

- mario_red_lights_group (arms, T-shirt) and mario_blue_lights_group (pants, overall bib) become black
- the bib and buttons are drawn under a new white light (the shirt front)
- caps (on head, in hand, unused) switch to a new red mario_cap_lights_group, which is also set before
  every non-metal M-logo draw, since those inherit whatever light came before
- the overall button texture becomes a black shirt stud

usage: mario_suit.py <sm64 repo root> [--button-only]

The model.inc.c edits are already committed in this repo; setup.sh runs this with --button-only to
recolour the overall button texture, which is extracted from the player's own ROM.
"""
import os, re, sys
from PIL import Image

root = sys.argv[1]
button_only = '--button-only' in sys.argv[2:]
path = os.path.join(root, 'actors/mario/model.inc.c')
def edit_model():
    global n_torso, n_cap, n_logo
    lines = open(path).read().split('\n')

    CAP_DLS = {'mario_face_cap_on_dl', 'mario_low_poly_face_cap_on_dl', 'mario_cap_unused_dl'}
    TORSO_DLS = {'mario_torso', 'mario_medium_poly_torso', 'mario_low_poly_torso'}
    logo = re.compile(r'^(\s*)gsSPDisplayList\((mario_m_logo_dl|mario_low_poly_mario_m_logo_dl|mario_right_hand_cap_m_logo_dl)\),$')
    red = re.compile(r'^(\s*gsSPLight\(&)mario_red_lights_group(\.[la], [12]\),)$')

    out, cur = [], None
    n_logo = n_cap = n_torso = 0
    for line in lines:
        m = re.match(r'^const Gfx (\w+)\[\]', line)
        if m:
            cur = m.group(1)
            out.append(line)
            if cur in TORSO_DLS:
                out += ['    gsSPLight(&mario_shirt_lights_group.l, 1),', '    gsSPLight(&mario_shirt_lights_group.a, 2),']
                n_torso += 1
            continue
        lm = logo.match(line)
        if lm and 'metal' not in cur:
            out += [lm.group(1) + 'gsSPLight(&mario_cap_lights_group.l, 1),', lm.group(1) + 'gsSPLight(&mario_cap_lights_group.a, 2),']
            n_logo += 1
        r = red.match(line)
        if r and cur in CAP_DLS:
            line = r.group(1) + 'mario_cap_lights_group' + r.group(2)
            n_cap += 1
        out.append(line)
    text = '\n'.join(out)

    def swap(old, new):
        global text
        assert text.count(old) == 1, old
        text = text.replace(old, new)

    swap("""static const Lights1 mario_blue_lights_group = gdSPDefLights1(
        0x00, 0x00, 0x7f,
        0x00, 0x00, 0xff, 0x28, 0x28, 0x28
    );""", """// GE64: suit trousers (was blue overalls)
    static const Lights1 mario_blue_lights_group = gdSPDefLights1(
        0x08, 0x08, 0x0a,
        0x1c, 0x1c, 0x22, 0x28, 0x28, 0x28
    );""")
    swap("""static const Lights1 mario_red_lights_group = gdSPDefLights1(
        0x7f, 0x00, 0x00,
        0xff, 0x00, 0x00, 0x28, 0x28, 0x28
    );""", """// GE64: suit jacket (was the red shirt and arms)
    static const Lights1 mario_red_lights_group = gdSPDefLights1(
        0x08, 0x08, 0x0a,
        0x1c, 0x1c, 0x22, 0x28, 0x28, 0x28
    );

    // GE64: white shirt front (the old overall bib) and Mario's own red cap
    static const Lights1 mario_shirt_lights_group = gdSPDefLights1(
        0x70, 0x70, 0x70,
        0xe8, 0xe8, 0xe8, 0x28, 0x28, 0x28
    );

    static const Lights1 mario_cap_lights_group = gdSPDefLights1(
        0x7f, 0x00, 0x00,
        0xff, 0x00, 0x00, 0x28, 0x28, 0x28
    );""")
    open(path, 'w').write(text)

n_torso = n_cap = n_logo = 0
if not button_only:
    edit_model()

png = os.path.join(root, 'actors/mario/mario_overalls_button.rgba16.png')
im = Image.open(png).convert('RGBA')
px = im.load()
opaque = [px[x, y] for y in range(im.height) for x in range(im.width) if px[x, y][3]]
if all(b - r == 2 and r == g for r, g, b, a in opaque):
    print('overall button already recoloured')
    sys.exit(0)
for y in range(im.height):
    for x in range(im.width):
        r, g, b, a = px[x, y]
        if a:
            v = (r + g + b) // 3
            px[x, y] = (v // 8 + 10, v // 8 + 10, v // 8 + 12, a)
im.save(png)
print('torso shirt lights: %d, cap light swaps: %d, logo light inserts: %d' % (n_torso, n_cap, n_logo))
