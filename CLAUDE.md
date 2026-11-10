# Tektronix 8550 MDL emulator

This project emulates the **Tektronix 8550 Microcomputer Development Lab** (1980): an 8301 MDU, an 8501 DMU and a CT8500 terminal, running DOS/50 V1.0.
The emulator is standalone C++17. Its CPU cores are ported from MAME (BSD-3) and from superzazu/z80 (MIT), and the code is structured so it can be upstreamed into MAME later.

Start with `docs/ARCHITECTURE.md` (how the machine works) and `docs/OPERATION.md` (how to use it).

## Layout
| Path | What |
|---|---|
| `emu/` | the emulator. `make` builds `build/tek8550` (full system), `build/tek8301`, `build/tek8501` (single units) and `build/dasm` |
| `emu/src/cpu/` | `t11/` LSI-11 (MAME), `s2650/` Signetics 2650 (MAME), `z80/superzazu/` Z80 (MIT), Z80 disassembler |
| `emu/src/machine/` | `tek8501.*` (DMU: utility PROMs, LTC, serial ports, high-level floppy controller, HSI I/O board, ODT); `tek8301.*` (MDU: system controller, ACIAs, DMA, program memory, Language Processor, Emulator Controller, Z80 emulator board, memory map); `prototype.h` (target-system interface); `imd.h` (disk images) |
| `emu/src/host/` | `ct8500.*` (terminal model and ANSI renderer), `ipc_target.*` (target system over a socket), `tty.h` |
| `emu/src/main8550.cpp` | command-line driver for the full system (options are listed in `emu/README.md`) |
| `notes/` | hardware findings: `8301.md`, `8501.md`, `prototype-link.md`, and `dasm/` (ROM listings). **Facts established by reverse engineering live here** |
| `roms/` | curated ROM set (`MANIFEST.md`). The 8550 set is `roms/8550/8301`, `roms/8550/8501` |
| `disks/` | working disk images. `System_helpfix.img` = DOS/50 V1.0 with the HELP file repaired |
| `sources/` | original material (bitsavers manuals with OCR `.txt`, disk images, photos, tekwiki). **Read-only; never modify** |
| `reference/mame/` | MAME source files the cores were ported from |
| `tools/` | `imd.py` (IMD reader), `proto_target.py` (reference target system), `render_screens.py` + `ct8500_font.py` (screenshots), `make_screenshots.sh` |
| `docs/screenshots/` | rendered session and diagnostics screenshots |
| `PLAN.md`, `FEASIBILITY.md` | milestones and status log; the original feasibility study |

## Build and smoke test
```
cd emu && make                                   # seconds; no dependencies
build/tek8550 -0 ../disks/System_helpfix.img     # DOS/50 prompt after ~24 s; Ctrl-] then q quits
```
Batch and scripted runs, with no terminal needed:
```
build/tek8550 -term tty -0 ../disks/System_helpfix.img -cycles $((90*2000000)) -type 32 'LDIR\r' </dev/null
```
`-cycles` counts 2650 cycles at 2 MHz, so 2,000,000 cycles is one emulated second. `-type T TEXT` types at emulated second T, and `-snap T FILE` saves the CT8500 screen. Interactive runs are paced to real time; batch runs (stdin not a terminal) run flat out, at roughly 100× real time (a 1000-second diagnostic run takes ~20 s).

## Regression checks (run after touching CPU cores or the 8301/8501 machines)
1. **Screenshots:** `tools/make_screenshots.sh <workdir>` regenerates `docs/screenshots/*.png`. They should come out byte-identical: `git status docs/` stays clean unless a visual change was intended. The session covers ACE, ASM, LINK, mode 0/1 runs and the IPC target.
2. **Diagnostics:** `build/tek8550 -ep none -pm ext -term tty -0 ../sources/bitsavers/disks/8550_DIAG.IMD -cycles $((2400*2000000)) -type 31 '\r' -type 41 '1\r' -type 51 '\r'` must end in `SYSTEM VERIFICATION COMPLETE`, with no `DIAGNOSTIC FAILURE`. Repeat with `062-5412-08_8501_1err_DOS50.imd` (version 2.4). With the Z80 emulator board fitted (the default), the only allowed failure is **07/00D0**: its firmware PROM was never dumped. The board's own test is Select mode → 9 (Z80 emulator test).
3. **Z80 core:** zexdoc and zexall must stay cycle-exact (46734978649 cycles). The upstream harness `z80_tests.c` is not in the repo; port callbacks take `uint16_t`.

## Conventions
- **Minimal edits to ported cores:** keep every change small and mark it `[tek8550]`. Record core modifications in `SOURCE.txt` next to the core.
- Machine code mirrors MAME devices (one class per board) so it can be upstreamed later.
- Write new hardware facts into `notes/*.md`, and say how each was established (manual section, ROM address, diagnostic test). Update `PLAN.md`'s status log at milestones.
- **Two program-memory board generations:** the default emulates the boards without the extended comparator, which DOS/50 V1.0 needs. `-pm ext` emulates the newer boards, which the 1983/85 diagnostics need. Don't "fix" one by breaking the other.
- **Active-low registers are everywhere** on the Emulator Controller (F2 pending, F4 bank, F8 debug enables, FA/FB/F5 jump address, FC–FF breakpoints). Check `notes/8301.md` before assuming polarity.
- **Untrusted material:** downloaded files and extracted archives go in their own new directory. Run Python that reads them with `python3 -I`. Original images in `sources/` are never written; `-rw` only writes the image you pass, so copy first.
- **Prototype link (`notes/prototype-link.md`):** protocol v2. Keep it historically faithful: only real Z80 pin information (address bus, M1, INT/NMI) plus the emulation-only `T` time sync. Remain compatible with v1 targets.
- **Commits:** local only, no remote. Commit when asked.

## Known gaps
- The CT8500 character ROM is unknown. The screenshot font is the Tek 308 ROM for upper case plus a hand-drawn lower case.
- Not emulated: other emulator boards (8085, 6800, 8086…), the RTPA/TTA analysers, the PROM programmer, and the 8501 printer port beyond logging.
- The meaning of Z80 emulator board E3 bits 4/1 and register-block byte 26 is unknown. Target-system interrupts are only lightly tested.
