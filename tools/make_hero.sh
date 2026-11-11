#!/bin/bash
# Render docs/screenshots/hero.png: the graphical front end (CT8500 + 8301/8501 panels)
# during a DOS/50 session, composed on a backdrop.
# usage: tools/make_hero.sh [WORKDIR]   (needs emu/build/tek8550-gui and python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W"
cd "$ROOT/emu"
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/tek8550-gui -scale 1.25 -0 ../disks/System_helpfix.img \
	-cycles $((46*2000000)) -type 30 'LDIR\r' -type 38 'LDIR 8301/COMMANDS\r' -shot ${HERO_T:-38.4} "$W/hero.bmp" </dev/null >/dev/null 2>&1
python3 -I - "$W" "$ROOT/docs/screenshots/hero.png" <<'PY'
import sys
from PIL import Image, ImageFilter, ImageDraw
w, out = sys.argv[1], sys.argv[2]
term = Image.open(w + '/hero.bmp').convert('RGB')
p1 = Image.open(w + '/hero_8301.bmp').convert('RGB')
p2 = Image.open(w + '/hero_8501.bmp').convert('RGB')
pad, gap = 56, 28
stack_h = p1.height + gap + p2.height
H = max(term.height, stack_h) + 2 * pad
W = pad + term.width + gap + max(p1.width, p2.width) + pad
bg = Image.new('RGB', (W, H), (22, 23, 26))
# soft vertical light on the backdrop
g = Image.new('L', (1, H)); g.putdata([int(36 - 24 * y / H) for y in range(H)])
bg.paste(Image.merge('RGB', (g, g, g)).resize((W, H)), (0, 0), Image.new('L', (W, H), 90))
def place(img, x, y):
    sh = Image.new('L', (W, H), 0)
    ImageDraw.Draw(sh).rounded_rectangle([x + 6, y + 10, x + img.width + 6, y + img.height + 10], 12, fill=170)
    sh = sh.filter(ImageFilter.GaussianBlur(14))
    bg.paste((0, 0, 0), (0, 0), sh)
    m = Image.new('L', img.size, 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, img.width - 1, img.height - 1], 10, fill=255)
    bg.paste(img, (x, y), m)
ty = pad + (H - 2 * pad - term.height) // 2
place(term, pad, ty)
px = pad + term.width + gap
py = pad + (H - 2 * pad - stack_h) // 2
place(p1, px, py); place(p2, px, py + p1.height + gap)
bg.save(out, optimize=True)
print(out, bg.size)
PY
