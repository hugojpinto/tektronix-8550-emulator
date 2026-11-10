#!/usr/bin/env python3
"""Render CT8500 screen snapshots (tek8550 -snap) or plain text to PNG.

--style ct8500 (default): the CT8500's documented raster -- characters on a 6x8 dot
  matrix in an 8x10 dot field, 80x25 -> 640 dots x 250 scan lines, green on black;
  each scan line is doubled (second line dimmer) -> 640x500 for the tube's aspect.
  Glyphs: tools/ct8500_font.py -- Tek 308 ROM upper case + matching hand-drawn lower case
  with descenders (approximation; the CT8500 character ROM is not dumped).
--style modern: a TrueType monospace font in --cell WxH cells (default 10x20 -> 800x500).
--crt: present the raster on a CRT -- overscan border, phosphor glow, rounded tube face,
  edge vignette and a bezel (the raster itself is not rescaled).
usage: render_screens.py [--style ct8500|modern] [--crt] [--cell WxH] [--font TTF] INPUT OUTPUT.png
INPUT is a .scr snapshot ("CT8500 ..." header) or a plain text file (last 25 lines).
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

FG = (214, 230, 214)      # CT8500-style pale phosphor
DIM = (130, 150, 130)
BG = (4, 8, 5)

def load(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    if lines and lines[0].startswith('CT8500 '):
        _, cols, rows, cr, cc, con = lines[0].split()
        cols, rows = int(cols), int(rows)
        text = [l.ljust(cols)[:cols] for l in lines[1:1 + rows]]
        attr = [l.ljust(cols, '0')[:cols] for l in lines[1 + rows:1 + 2 * rows]]
        return cols, rows, text, attr, (int(cr), int(cc)) if con == '1' else None
    body = [l.rstrip('\r') for l in lines]
    while body and body[-1] == '': body.pop()
    body = body[-25:]
    text = [l.ljust(80)[:80] for l in body] + [' ' * 80] * (25 - len(body))
    return 80, 25, text, ['0' * 80] * 25, None

GREEN = (70, 255, 120); GREEN_DIM = (35, 150, 70)

def render_ct8500(cols, rows, text, attr, cursor, out):
    from ct8500_font import glyph
    W, H = cols * 8, rows * 10                       # 640 x 250 dot raster
    dots = [[0] * W for _ in range(H)]               # 0 off, 1 dim, 2 bright
    for y in range(rows):
        for x in range(cols):
            a = int(attr[y][x], 16)
            lvl = 1 if a & 8 else 2
            g = glyph(ord(text[y][x]))
            cell = [[0] * 8 for _ in range(10)]
            for gy in range(8):                       # 6x8 matrix (5 dots + spacing) at field rows 1-8
                for gx in range(5):
                    if g[gy] >> (4 - gx) & 1: cell[1 + gy][1 + gx] = lvl
            if a & 1:                                 # underline on the last scan line of the field
                for cx in range(8): cell[9][cx] = lvl
            inv = bool(a & 4) ^ (cursor == (y, x))    # inverse video / block cursor
            for cy in range(10):
                for cx in range(8):
                    v = cell[cy][cx]
                    if inv: v = 0 if v else lvl
                    dots[y * 10 + cy][x * 8 + cx] = v
    img = Image.new('RGB', (W, H * 2), BG)
    px = img.load()
    for yy in range(H):
        for xx in range(W):
            v = dots[yy][xx]
            if v:
                c = GREEN if v == 2 else GREEN_DIM
                px[xx, yy * 2] = c
                px[xx, yy * 2 + 1] = tuple(int(k * 0.55) for k in c)
    img.save(out)
    print(out, img.size)

def crt_frame(img, overscan=36, bezel=44, radius=48):
    """Place a rendered raster on a CRT face inside a bezel."""
    W, H = img.size
    sw, sh = W + 2 * overscan, H + 2 * overscan                 # glass incl. overscan
    glass = Image.new('RGB', (sw, sh), BG)                      # same black as the raster: no visible box
    glass.paste(img, (overscan, overscan))
    # phosphor bloom: blurred copy added back
    bloom = glass.filter(ImageFilter.GaussianBlur(3)).point(lambda v: int(v * 0.55))
    glass = ImageChops.add(glass, bloom)
    halo = glass.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.25))
    glass = ImageChops.add(glass, halo)
    # vignette: darker towards the edges and corners of the tube
    vig = Image.new('L', (sw, sh), 0)
    vd = ImageDraw.Draw(vig)
    steps = 40
    for i in range(steps):
        t = i / (steps - 1)
        shade = int(150 + 105 * t)
        inset_x, inset_y = int(sw * 0.16 * (1 - t)), int(sh * 0.16 * (1 - t))
        vd.rounded_rectangle([inset_x, inset_y, sw - inset_x, sh - inset_y], radius=radius, fill=shade)
    vig = vig.filter(ImageFilter.GaussianBlur(25))
    glass = Image.composite(glass, Image.new('RGB', (sw, sh), (0, 0, 0)), vig)
    # faint glass reflection in the upper left
    refl = Image.new('L', (sw, sh), 0)
    ImageDraw.Draw(refl).ellipse([-sw * 0.25, -sh * 0.6, sw * 0.55, sh * 0.35], fill=14)
    refl = refl.filter(ImageFilter.GaussianBlur(40))
    glass = ImageChops.add(glass, Image.merge('RGB', (refl, refl, refl)))
    # rounded tube face mask
    mask = Image.new('L', (sw, sh), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, sw - 1, sh - 1], radius=radius, fill=255)
    # bezel: dark plastic with a lighter inner lip
    fw, fh = sw + 2 * bezel, sh + 2 * bezel
    frame = Image.new('RGB', (fw, fh), (34, 33, 31))
    fd = ImageDraw.Draw(frame)
    fd.rounded_rectangle([6, 6, fw - 7, fh - 7], radius=radius + 26, outline=(52, 50, 47), width=3)
    fd.rounded_rectangle([bezel - 10, bezel - 10, bezel + sw + 9, bezel + sh + 9], radius=radius + 10, fill=(18, 18, 17))
    fd.rounded_rectangle([bezel - 10, bezel - 10, bezel + sw + 9, bezel + sh + 9], radius=radius + 10, outline=(58, 56, 52), width=2)
    frame.paste(glass, (bezel, bezel), mask)
    return frame

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--crt', action='store_true')
    ap.add_argument('input'); ap.add_argument('output')
    ap.add_argument('--style', default='ct8500', choices=['ct8500', 'modern'])
    ap.add_argument('--cell', default='10x20')
    ap.add_argument('--font', default='/System/Library/Fonts/Menlo.ttc')
    args = ap.parse_args()
    cols, rows, text, attr, cursor = load(args.input)
    if args.style == 'ct8500':
        render_ct8500(cols, rows, text, attr, cursor, args.output)
        if args.crt:
            img = crt_frame(Image.open(args.output).convert('RGB'))
            img.save(args.output); print(args.output, img.size, 'crt')
        return
    cw, chh = (int(v) for v in args.cell.split('x'))
    font = ImageFont.truetype(args.font, int(chh * 0.8))
    img = Image.new('RGB', (cols * cw, rows * chh), BG)
    d = ImageDraw.Draw(img)
    for y in range(rows):
        for x in range(cols):
            a = int(attr[y][x], 16)
            fg, bg = (DIM if a & 8 else FG), BG
            if a & 4: fg, bg = bg, fg                      # inverse
            if cursor and cursor == (y, x): fg, bg = bg, FG  # block cursor
            if bg != BG: d.rectangle([x * cw, y * chh, (x + 1) * cw - 1, (y + 1) * chh - 1], fill=bg)
            chr_ = text[y][x]
            if chr_ != ' ': d.text((x * cw, y * chh + chh * 0.08), chr_, font=font, fill=fg)
            if a & 1: d.line([x * cw, (y + 1) * chh - 2, (x + 1) * cw - 1, (y + 1) * chh - 2], fill=fg)
    img.save(args.output)
    print(args.output, img.size)

if __name__ == '__main__':
    main()
