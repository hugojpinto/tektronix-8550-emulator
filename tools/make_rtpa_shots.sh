#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Render docs/screenshots/rtpa/*.png: the pictures in docs/RTPA.md. One DOS/50 session on the
# Z80 emulator with the RTPA fitted types the tutorial's commands in order (the sample program,
# then sections 3 to 10) and snaps the CT8500 screen at the key moments. Batch runs are
# deterministic, so the pictures come out the same on every run.
# usage: tools/make_rtpa_shots.sh [WORKDIR]   (needs emu/build/tek8550, python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W"
OUT="$ROOT/docs/screenshots/rtpa"; mkdir -p "$OUT"
cd "$ROOT/emu"
cp ../disks/System_Fixed.img "$W/rtpa.img"

A=(); t=30; SHOTS=()
# c TEXT [SECONDS]: type a line at the prompt, then wait (2 s by default)
c() { A+=(-type "$t" "$1\r"); t=$((t + ${2:-2})); }
# g TEXT: a line that runs the program or lists the trace, given more time
g() { c "$1" 4; }
# s NAME: snap the screen now
s() { A+=(-snap "$t" "$W/$1.scr"); SHOTS+=("$1"); t=$((t + 1)); }

c 'SEL Z80' 8   # it loads from disc
# 2. the sample program
c 'F 200 400 FF'
c 'P 200 F3 11 75 02 21 50 02 36 00 7D BB C2 11 02 C3 75'
c 'P 210 03 23 C3 07 02'
c 'P 300 00 76'
c 'P 375 01 25 03 21 50 03 36 00 7D B9 C2 85 03 C3 00 03'
c 'P 385 2B C3 7B 03'
# 3. stop on an event
c 'EVT 1 CLR A=201 D=11'; c 'BIF 1'; g 'EVT'; g 'GO 200'; s r1_break
c 'EVT 1 B=W'; g 'GO 200'
c 'EVT 1 B=ALL'; c 'EVT 2 CLR A=300 D=00'; c 'BIF CLR'; c 'BIF 2'; g 'GO 200'
# 4. the trace
g 'DRT 5'; s r2_drt
c 'EVT 2 B=F'; c 'RTT F'; g 'GO 200'; g 'DRT 5'
# 5. count and time
c 'RTT ALL'; c 'EVT CLR'; c 'EVT 1 A=209 D=7D B=F'; c 'BIF 1'; c 'CNT C'; g 'GO 200'; g 'CNT'
c 'CNT U'; g 'GO 200'; g 'CNT'; s r3_count
# 6. pass counts and delays
c 'EVT 1 P=2'; c 'CNT C'; g 'GO 200'; g 'CNT'
c 'EVT 1 P=20'; g 'GO 200'; g 'CNT'
c 'EVT 1 C=500'; g 'GO 200'; g 'CNT'; s r4_delay
c 'EVT 2 A=37E D=0B9 B=MR'; c 'BIF CLR'; c 'BIF 2'; g 'GO 200'; g 'CNT'
c 'EVT 2 P=10'; g 'GO 200'; g 'CNT'
# 7. count loop passes
c 'EVT 1 CLR A=207 D=36'; c 'EVT 2 CLR A=300 D=00'; c 'CNT 1'; g 'GO 200'; g 'CNT'; s r5_loops
# 8. ARM
c 'EVT 1 CLR A=207 D=36'; c 'EVT 2 CLR A=37B D=36'; c 'BIF ARM'; c 'CNT C'; g 'GO 200'; g 'CNT'; s r6_arm
c 'EVT 1 CLR A=207 D=36'; c 'EVT 2 CLR A=207 D=36'; g 'GO 200'; g 'CNT'
# 9. FRZ
c 'EVT 1 CLR A=209 D=7D'; c 'EVT 2 CLR A=300 D=00'; c 'BIF FRZ'; c 'CNT C'; g 'GO 200'; g 'DRT 11'; g 'CNT'; s r7_frz
# 10. LIM
c 'EVT 1 CLR A>200 D=0C3'; c 'EVT 2 CLR A<300'; c 'BIF LIM'; g 'GO 200'
c 'EVT 2 A>300'; g 'GO 200'; s r8_lim

build/tek8550 -rtpa -term tty -0 "$W/rtpa.img" -cycles $((t * 2000000)) "${A[@]}" </dev/null >/dev/null 2>&1
for n in "${SHOTS[@]}"; do
	python3 -I "$ROOT/tools/render_screens.py" --crt "$W/$n.scr" "$OUT/$n.png" >/dev/null; echo "$OUT/$n.png"
done
