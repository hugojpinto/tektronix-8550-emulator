#!/usr/bin/env python3
"""Compose tek8550-gui window captures (-shot X.bmp writes X.bmp, X_8301.bmp, X_8501.bmp)
into one picture on a dark backdrop with soft shadows.
usage: compose_gui.py SHOT.bmp OUT.png [--width W]   (W: scale the result to this width)"""
import sys, os
from PIL import Image, ImageFilter, ImageDraw

def compose(shot, out, width=None):
    base = os.path.splitext(shot)[0]
    term = Image.open(shot).convert('RGB')
    panels = [Image.open(base + '_' + t + '.bmp').convert('RGB') for t in ('8301', '8501') if os.path.exists(base + '_' + t + '.bmp')]
    pad, gap = 56, 28
    stack_h = sum(p.height for p in panels) + gap * max(len(panels) - 1, 0)
    H = max(term.height, stack_h) + 2 * pad
    W = pad + term.width + (gap + max(p.width for p in panels) if panels else 0) + pad
    bg = Image.new('RGB', (W, H), (22, 23, 26))
    g = Image.new('L', (1, H)); g.putdata([int(36 - 24 * y / H) for y in range(H)])
    bg.paste(Image.merge('RGB', (g, g, g)).resize((W, H)), (0, 0), Image.new('L', (W, H), 90))
    def place(img, x, y):
        sh = Image.new('L', (W, H), 0)
        ImageDraw.Draw(sh).rounded_rectangle([x + 6, y + 10, x + img.width + 6, y + img.height + 10], 12, fill=170)
        bg.paste((0, 0, 0), (0, 0), sh.filter(ImageFilter.GaussianBlur(14)))
        m = Image.new('L', img.size, 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, img.width - 1, img.height - 1], 10, fill=255)
        bg.paste(img, (x, y), m)
    place(term, pad, pad + (H - 2 * pad - term.height) // 2)
    y = pad + (H - 2 * pad - stack_h) // 2
    for p in panels:
        place(p, pad + term.width + gap, y); y += p.height + gap
    if width and bg.width > width:
        bg = bg.resize((width, int(bg.height * width / bg.width)), Image.LANCZOS)
    bg.save(out, optimize=True)
    print(out, bg.size)

if __name__ == '__main__':
    a = sys.argv[1:]
    w = None
    if '--width' in a: i = a.index('--width'); w = int(a[i + 1]); del a[i:i + 2]
    compose(a[0], a[1], w)
