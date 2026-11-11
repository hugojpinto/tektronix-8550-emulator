#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Render docs/screenshots/tta/*.png, the pictures in docs/TTA.md: TTA.TST V1.3A on the emulated
# 8550 (Diagnostic System 3.0, 8550_DIAG.IMD). Batch runs are deterministic, so the pictures
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
wait
for s in 01_programs 02_options 03_modules 04_option0_pass 05_module44 06_board_absent 07_probe 08_probe_fixture; do crt $s; done
