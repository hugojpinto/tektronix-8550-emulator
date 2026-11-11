#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Render docs/screenshots/tta/*.png: the tutorial pictures in docs/TTA.md (u*.png, one DOS/50
# session with the TTA commands) and the diagnostic pictures in docs/OPERATION.md §10 (0*.png,
# TTA.TST V1.3A on 8550_DIAG.IMD). Batch runs are deterministic, so the pictures
# come out the same on every run.
# usage: tools/make_tta_shots.sh [WORKDIR]   (needs emu/build/tek8550, python3 + Pillow)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="${1:-$(mktemp -d)}"; mkdir -p "$W"
OUT="$ROOT/docs/screenshots/tta"; mkdir -p "$OUT"
cd "$ROOT/emu"
D=../sources/bitsavers/disks/8550_DIAG.IMD
EMU() { build/tek8550 -ep none -pm ext -term tty -0 "$D" "$@" </dev/null >/dev/null 2>&1; }
crt() { python3 -I "$ROOT/tools/render_screens.py" --crt "$W/$1.scr" "$OUT/$1.png" >/dev/null; echo "$OUT/$1.png"; }
# Select mode 2, then program 6 (TRIGGER TRACE ANALYZER TEST)
SEL=(-type 31 '2\r' -type 41 '6\r')
# after an option: 3 = do not loop on error, then display on the terminal
RUN=(-type 56 '3\r' -type 61 '\r' -type 66 '\r')

# 1-3: the menus (program list, TTA.TST options, the module list from H)
EMU -tta -cycles $((56*2000000)) -type 31 '2\r' -snap 40 "$W/01_programs.scr" -type 41 '6\r' \
	-snap 50 "$W/02_options.scr" -type 51 'H\r' -snap 55 "$W/03_modules.scr" &
# 4: option 0, all modules 00-60, after about 50 passes
EMU -tta -cycles $((300*2000000)) "${SEL[@]}" -type 51 '\r' "${RUN[@]}" -snap 299 "$W/04_option0_pass.scr" &
# 5: one module (T 44, the dynamic counter test) over and over
EMU -tta -cycles $((100*2000000)) "${SEL[@]}" -type 51 'T\r' -type 56 '44\r' -type 61 '3\r' -type 66 '\r' -type 71 '\r' \
	-snap 99 "$W/05_module44.scr" &
# 6: the same diagnostic without -tta
EMU -cycles $((73*2000000)) "${SEL[@]}" -type 51 '\r' "${RUN[@]}" -snap 72 "$W/06_board_absent.scr" &
# 7: option 7, probe interface, with nothing clipped on the probe
EMU -tta -cycles $((90*2000000)) "${SEL[@]}" -type 51 '7\r' "${RUN[@]}" -snap 89 "$W/07_probe.scr" &
# 8: option 7 with the inferred fixture (-tta probe: the probe on the extended address lines)
build/tek8550 -tta probe -ep none -pm ext -term tty -0 "$D" -cycles $((120*2000000)) "${SEL[@]}" -type 51 '7\r' "${RUN[@]}" -type 71 '\r' \
	-snap 119 "$W/08_probe_fixture.scr" </dev/null >/dev/null 2>&1 &
# the tutorial (docs/TTA.md): the TTA manual's learning guide, on the Z80 board
cp ../disks/System_Fixed.img "$W/tut.img"
T=(-type 30 'SEL Z80\r' -type 36 'P 100 21 00 05 06 05 AF 86 23 05 C2 06 01 18 FE\r' -type 39 'P 500 01 02 03 04 05\r'
   -type 41 'BK 10C\r' -type 43 'GO 100\r' -snap 47 "$W/u1_program.scr"
   -type 48 'disp\r' -snap 53 "$W/u2_disp.scr"
   -type 54 'ad 1 106\r' -type 56 'bre 1\r' -type 58 'GO 100\r' -snap 62 "$W/u3_break.scr"
   -type 63 'bre 1 cont\r' -type 65 'GO 100\r' -snap 70 "$W/u4_cont.scr"
   -type 71 'ad 1 clr\r' -type 72 'bre 1 clr\r' -type 74 'P 503 00\r' -type 76 'data 1 86\r' -type 78 'bre 1 cont\r'
   -type 80 'GO 100\r' -snap 86 "$W/u5_bug.scr"
   -type 87 'data 1 clr\r' -type 88 'bre 1 clr\r' -type 89 'data 3 86\r' -type 91 'bus 4 m rd\r' -type 93 'cons cyc 34\r'
   -type 95 'acq ev4\r' -type 97 'GO 100\r' -type 101 'disp\r' -snap 106 "$W/u6_operands.scr"
   -type 107 'tclr -x\r' -type 108 'P 503 04\r' -type 110 'ad 1 106\r' -type 112 'cou 2 v=3 s=ev1 o=delay\r' -type 114 'bre 2\r'
   -type 116 'GO 100\r' -snap 120 "$W/u7_pass.scr"
   -type 121 'tclr -x\r' -type 122 'eve 1 a=100 b=f\r' -type 124 'cou 1 s=ev1 o=delay v=1\r' -type 126 'eve 2 a=10C b=f\r'
   -type 128 'cou 2 s=ev2 o=delay v=1\r' -type 130 'eve 3 c=10xx\r' -type 132 'cou 3 g=self o=arm v=0 s=200nsec\r'
   -type 134 'GO 100\r' -type 138 'ts 3\r' -snap 142 "$W/u8_timing.scr")
build/tek8550 -tta -term tty -0 "$W/tut.img" -cycles $((143*2000000)) "${T[@]}" </dev/null >/dev/null 2>&1 &
wait
for s in u1_program u2_disp u3_break u4_cont u5_bug u6_operands u7_pass u8_timing; do crt $s; done
for s in 01_programs 02_options 03_modules 04_option0_pass 05_module44 06_board_absent 07_probe 08_probe_fixture; do crt $s; done
