#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Regenerate docs/screenshots: a scripted 8550 session (edit with ACE, assemble,
# list, link, run in emulation mode 1 against the reference target) captured
# from the emulated CT8500 at its native 80x25 grid.
# usage: tools/make_screenshots.sh [WORKDIR]   (needs emu/build/tek8550, python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W/shots" "$ROOT/docs/screenshots"
cp "$ROOT/disks/System_Fixed.img" "$W/demo.img"
PORT=18573
python3 -I "$ROOT/tools/proto_target.py" --tcp $PORT < /dev/null > "$W/shots/08_target_console.txt" 2> "$W/target.log" &
TPID=$!; sleep 1
args=(-snap 31 "$W/shots/01_boot.scr" -type 32 'SEL Z80\r' -type 45 'ACE HELLO.ASM\r' -snap 80 "$W/shots/02_ace_new.scr" -type 82 '\e\x0c')
t=84
while IFS= read -r line; do args+=(-type $t "$line\\r"); t=$((t+2)); done <<'SRC'
; HELLO -- prints a greeting on the console port of the target
        ORG     100H
START   LD      HL,MSG
LOOP    LD      A,(HL)
        OR      A
        JR      Z,DONE
        OUT     (10H),A
        INC     HL
        JR      LOOP
DONE    JR      DONE
MSG     ASCII   "HELLO FROM THE 8550"
        BYTE    0DH,0AH,0
        END     START
SRC
args+=(-type $((t+2)) '\e' -type $((t+5)) '\e\x54' -type $((t+7)) '\e\x0d' -snap $((t+10)) "$W/shots/03_ace_edited.scr" -type $((t+12)) 'EX\r'
       -type $((t+30)) 'ASM HELLO.OBJ HELLO.LST HELLO.ASM\r' -snap $((t+100)) "$W/shots/04_asm.scr"
       -type $((t+102)) 'COPY HELLO.LST CONO\r' -snap $((t+120)) "$W/shots/05_listing.scr"
       -type $((t+122)) 'LINK HELLO.LOAD HELLO.LNKL HELLO.OBJ\r' -snap $((t+190)) "$W/shots/06_link.scr"
       -type $((t+192)) 'EM 1\r' -type $((t+196)) 'LOAD HELLO.LOAD\r' -type $((t+210)) 'GO\r' -type $((t+218)) '\e\e'
       -type $((t+222)) 'DS\r' -snap $((t+228)) "$W/shots/07_run_mode1.scr")
(cd "$ROOT/emu" && ./build/tek8550 -rw -target tcp:127.0.0.1:$PORT -0 "$W/demo.img" -cycles $(( (t+230)*2000000 )) "${args[@]}" < /dev/null > /dev/null 2> "$W/emu.log")
sleep 1; kill $TPID 2>/dev/null || true
mkdir -p "$ROOT/docs/screenshots/raw"
for f in "$W"/shots/*.scr "$W/shots/08_target_console.txt"; do
	n=$(basename "${f%.*}")
	python3 -I "$ROOT/tools/render_screens.py" "$f" "$ROOT/docs/screenshots/raw/$n.png"        # bare raster
	python3 -I "$ROOT/tools/render_screens.py" --crt "$f" "$ROOT/docs/screenshots/$n.png"     # on a CRT
done
