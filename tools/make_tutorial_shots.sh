#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Render docs/screenshots/tutorials/*.png: progress pictures for docs/TUTORIALS.md.
# Tutorials 1 and 3 are captures of the graphical lab (terminal + front panels); tutorial 2 is
# the CT8500 screen rendered by render_screens.py. Batch runs are deterministic, so the pictures
# come out the same on every run.
# usage: tools/make_tutorial_shots.sh [WORKDIR]   (needs emu/build/tek8550 and tek8550-gui, python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W"
OUT="$ROOT/docs/screenshots/tutorials"; mkdir -p "$OUT"
cd "$ROOT/emu"
GUI() { SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software build/tek8550-gui -scale 1.25 "$@" </dev/null >/dev/null 2>&1; }
compose() { python3 -I "$ROOT/tools/compose_gui.py" "$W/$1.bmp" "$OUT/$1.png" --width 1400; }
crt() { python3 -I "$ROOT/tools/render_screens.py" --crt "$W/$1.scr" "$OUT/$1.png" >/dev/null; echo "$OUT/$1.png"; }

# 1. first boot in the graphical lab
cp ../disks/System_Fixed.img "$W/t1.img"
GUI -0 "$W/t1.img" -cycles $((37*2000000)) -type 30 'LDIR\r' \
	-shot 3 "$W/t1_selftest.bmp" -shot 29 "$W/t1_prompt.bmp" -shot 36 "$W/t1_ldir.bmp"
compose t1_selftest; compose t1_prompt; compose t1_ldir

# 2. a Z80 program from scratch (paced like typing)
cp ../disks/System_Fixed.img "$W/t2.img"
build/tek8550 -term tty -0 "$W/t2.img" -cycles $((306*2000000)) \
	-type 32 'SEL Z80\r' -type 45 'EDIT T.ASM\r' -type 52 'INPUT\r' \
	-type 54 '        ORG     100H\r' -type 56 'START   LD      B,10H\r' -type 58 'LOOP    INC     A\r' \
	-type 60 '        DJNZ    LOOP\r' -type 62 'DONE    JR      DONE\r' -type 64 '        END     START\r' \
	-type 66 '\r' -type 68 'FILE\r' -snap 72 "$W/t2_edit.scr" \
	-type 75 'ASM T.OBJ T.LST T.ASM\r' -type 160 'LINK T.LOAD T.LNKL T.OBJ\r' -snap 238 "$W/t2_asm_link.scr" \
	-type 240 'LOAD T.LOAD\r' -type 255 'BK 102\r' -type 260 'GO\r' -type 275 'DS\r' -type 285 'GO\r' -type 300 'DS\r' \
	-snap 305 "$W/t2_debug.scr" </dev/null >/dev/null 2>&1
crt t2_edit; crt t2_asm_link; crt t2_debug

# 3. format a blank disc and duplicate the system disc
cp ../disks/System_Fixed.img "$W/t3.img"; : > "$W/blank.img"
GUI -0 "$W/t3.img" -mount 30 1="$W/blank.img" -cycles $((2560*2000000)) \
	-type 34 'FORMAT /DEV/FLX1\r' -shot 50 "$W/t3_probe.bmp" -type 57 'Y\r' -shot 100 "$W/t3_format.bmp" \
	-type 200 'VERIFY /DEV/FLX1\r' \
	-type 1000 'COPY:L:B /DEV/FLX0 /DEV/FLX1\r' -type 1020 'Y\r' -shot 1500 "$W/t3_copy.bmp" -shot 2550 "$W/t3_done.bmp"
compose t3_probe; compose t3_format; compose t3_copy; compose t3_done
