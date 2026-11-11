#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Render docs/screenshots/hero.png: the graphical front end (CT8500 + 8301/8501 panels)
# during a DOS/50 session, composed on a backdrop.
# usage: tools/make_hero.sh [WORKDIR]   (needs emu/build/tek8550-gui and python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W"
cd "$ROOT/emu"
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/tek8550-gui -scale 1.25 -0 ../disks/System_helpfix.img \
	-cycles $((46*2000000)) -type 30 'LDIR\r' -type 38 'LDIR 8301/COMMANDS\r' -shot ${HERO_T:-38.4} "$W/hero.bmp" </dev/null >/dev/null 2>&1
python3 -I "$ROOT/tools/compose_gui.py" "$W/hero.bmp" "$ROOT/docs/screenshots/hero.png"
