#!/usr/bin/env python3
"""Render CT8500 screen snapshots (tek8550 -snap) or plain text to PNG.

Each image is the terminal's native 80x25 character grid at a fixed cell size
(default 10x20 px -> 800x500), no window chrome, no scaling.
usage: render_screens.py [--cell WxH] [--font TTF] [--title TEXT] INPUT OUTPUT.png
INPUT is a .scr snapshot ("CT8500 ..." header) or a plain text file (last 25 lines).
"""
import argparse, sys
from PIL import Image, ImageDraw, ImageFont

FG = (214, 230, 214)      # CT8500-style pale phosphor
DIM = (130, 150, 130)
BG = (12, 16, 12)

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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input'); ap.add_argument('output')
    ap.add_argument('--cell', default='10x20')
    ap.add_argument('--font', default='/System/Library/Fonts/Menlo.ttc')
    args = ap.parse_args()
    cw, chh = (int(v) for v in args.cell.split('x'))
    cols, rows, text, attr, cursor = load(args.input)
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
