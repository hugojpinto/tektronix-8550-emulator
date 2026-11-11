# Tektronix 8550 MDL emulator

This project emulates the **Tektronix 8550 Microcomputer Development Lab** (1980): an 8301 MDU, an 8501 DMU and a CT8500 terminal, running DOS/50 V1.0.
The emulator is standalone C++17. Its CPU cores are ported from MAME (BSD-3) and from superzazu/z80 (MIT), and the code is structured so it can be upstreamed into MAME later.

Start with `docs/ARCHITECTURE.md` (how the machine works) and `docs/OPERATION.md` (how to use it).

## Layout
| Path | What |
|---|---|
| `emu/` | the emulator. `make` builds `build/tek8550` (full system), `build/tek8301`, `build/tek8501` (single units) and `build/dasm` |
| `emu/src/cpu/` | `t11/` LSI-11 (MAME), `s2650/` Signetics 2650 (MAME), `z80/superzazu/` Z80 (MIT), `i8085/` 8085 (MAME), disassemblers |
| `emu/src/machine/` | `tek8501.*` (DMU: utility PROMs, LTC, serial ports, HSI I/O board, ODT, the old high-level floppy shim); `tek8501_fdc.*` (the Flexible Disc Controller board: Z80 + ROM 160-0412-00, drive interface, DMA); `flexdrive.*` (Qume DataTrak 8 drives, byte-cell tracks, image ↔ track conversion); `tek8301.*` (MDU: system controller, ACIAs, DMA, program memory, Language Processor, Emulator Controller, Z80 emulator board, memory map); `rtpa.*` (Real-Time Prototype Analyzer); `tta.*` (Trigger Trace Analyzer, in progress); `am9513.*` (AMD Am9513, generated from MAME by `emu/tools/port_am9513.py`); `prom_programmer.*`; `mc6850.h`; `prototype.h` (target-system interface); `imd.h` (disk images and `fd_geometry`) |
| `emu/src/host/` | `ct8500.*` (terminal model and ANSI renderer), `ipc_target.*` (target system over a socket), `serial_link.*` (REMOTE port over a socket), `tty.h` |
| `emu/src/system/` | `tek8550.*`: the whole lab as one class with no wall-clock notion (both units, HSI cable, links, scripted `-type`/`-snap`/`-poke`, named lamp outputs and inputs); `options.*`: the command line shared by every front end; `outputs.h` |
| `emu/src/main8550.cpp` | tty front end for the full system: ANSI CT8500, raw tty, monitor, real-time pacing (options are listed in `emu/README.md`) |
| `notes/` | hardware findings: `8301.md`, `8501.md`, `8501-fdc.md`, `prototype-link.md`, `host-link.md`, `rtpa.md`, `tta.md`, and `dasm/` (ROM listings). **Facts established by reverse engineering live here** |
| `roms/` | curated ROM set (`MANIFEST.md`). The 8550 set is `roms/8550/8301`, `roms/8550/8501` |
| `disks/` | working disk images. `System_helpfix.img` = DOS/50 V1.0 with the HELP file repaired |
| `sources/` | original material (bitsavers manuals with OCR `.txt`, disk images, photos, tekwiki). **Read-only; never modify** |
| `reference/mame/` | MAME source files the cores and devices were ported from (commit in `SOURCE.txt`) |
| `tools/` | `imd.py` (IMD reader), `proto_target.py` (reference target system), `host_link.py` (reference host for COMM/HOST) + `test_host_link.sh`, `render_screens.py` + `ct8500_font.py` (screenshots), `make_screenshots.sh` |
| `docs/screenshots/` | rendered session and diagnostics screenshots |
| `PLAN.md`, `FEASIBILITY.md` | milestones and status log; the original feasibility study |
| `../tekmato` | sibling tool for DOS/50 disc images (list/extract/write/check); the file-system format is in `../tekmato/FORMAT.md`. Use it to put files on a disc instead of typing them in through EDIT |

## Resuming work (state on 2026-11-11)
Most recent first:
1. **GUI front end (SDL3), next.** Plan in `docs/DESIGN-gui-and-fdc.md` §2: split the CT8500 model from its ANSI renderer, then `make gui` → `build/tek8550-gui` (terminal window, then front panels from MAME-style `.lay` files bound to the named outputs/inputs of `tek8550_system`). The headless build must stay dependency-free.
2. **Floppy controller: done, low level** (`notes/8501-fdc.md`). The real board's Z80 runs ROM 160-0412-00; `flexdrive.*` models the Qume drives and byte-cell tracks. `-fdc hle` keeps the old shim (byte-identical to before). Runtime discs: `-mount T N=FILE`, `-eject T N`, monitor `m`/`e`.
3. **System layer: done** (`emu/src/system/`): `tek8550_system` drives both units with no wall-clock notion; `main8550.cpp` is just the tty front end.
4. **TTA (Trigger Trace Analyzer), in progress.**
   - **State:** `-tta` fits it; TTA.TST (Select mode 6) modules 00, 10, 20–23 and 40–43 pass.
   - **Next:** decode the event comparator RAM wiring (modules 30/31), then counting from live sources (44–46), interrupt (50), acquisition (60) and probe. Start from `notes/tta.md`, which has the established facts, the RAM-initialisation analysis and the next steps.
   - **Caveat:** no software we have uses the TTA on the 8550 (DOS/50 V1.0 predates it), so the diagnostic is the only consumer.
5. **RTPA: done** (`notes/rtpa.md`). RTPA.TST options 0–7 pass (7 needs `-rtpa probe`), and DOS/50 EVT/BIF/CNT/RTT/DRT work. FRZ semantics are inferred, and INT 30/31 are unused.
6. **Host link: done** (`notes/host-link.md`, `-remote`, `tools/host_link.py`).
7. **Disc geometries and FORMAT: done** (all four 8501 formats, `notes/8501.md`; FORMAT re-verified on the real controller firmware). The sibling tool `../tekmato` handles them too.
8. **Prototype link v3: done**, verified jointly with FUSE (`notes/prototype-link.md`).

Still open beyond the TTA: see **Known gaps** below. The 8086, 6800 and 8080 boards and 8085 target modes are the larger items.

**How hardware has been reverse-engineered here:** run the board's diagnostic against a logging stub, read the expected values it prints, dump the diagnostic from memory (`-dump F`, then `build/dasm s2650 F.8301 0 2000 6000`) and read its table-driven tests. Then check against DOS/50's own command overlays (extract them with `../tekmato`) and verify in a scripted DOS/50 session.

**Debug switches** (environment variables, stderr):

| Variable | Logs |
|---|---|
| `TEK_IOTRACE8301` | 2650 I/O |
| `TEK_IOTRACE` | 8501 I/O (and every floppy command under `-fdc hle`) |
| `TEK_FDCTRACE` | floppy controller: each command block and its results (FDER, FDSR...) |
| `TEK_EPTRACE` | emulator board and forced jumps |
| `TEK_PCTRACE=lo-hi@sec` | 2650 instructions in a range, after a time |
| `TEK_INTTRACE` | interrupts taken |
| `TEK_RTPATRACE` | RTPA ports, matches and breaks |
| `TEK_TTATRACE` | TTA ports |
| `TEK_REMTRACE` | REMOTE port |
| `TEK_SLAVEIO` | LP/EP port I/O that goes nowhere |
| `TEK_PROMTRACE` | PROM programmer |

## Build and smoke test
```
cd emu && make                                   # seconds; no dependencies
build/tek8550 -0 ../disks/System_helpfix.img     # DOS/50 prompt after ~27 s; Ctrl-] then q quits
```
Batch and scripted runs, with no terminal needed:
```
build/tek8550 -term tty -0 ../disks/System_helpfix.img -cycles $((90*2000000)) -type 32 'LDIR\r' </dev/null
```
`-cycles` counts 2650 cycles at 2 MHz, so 2,000,000 cycles is one emulated second. `-type T TEXT` types at emulated second T, and `-snap T FILE` saves the CT8500 screen. Interactive runs are paced to real time; batch runs (stdin not a terminal) run flat out, at roughly 100× real time (a 1000-second diagnostic run takes ~20 s).

## Regression checks (run after touching CPU cores or the 8301/8501 machines)
1. **Screenshots:** `tools/make_screenshots.sh <workdir>` regenerates `docs/screenshots/*.png`. They should come out byte-identical: `git status docs/` stays clean unless a visual change was intended. The session covers ACE, ASM, LINK, mode 0/1 runs and the IPC target.
2. **Diagnostics:** `build/tek8550 -ep none -pm ext -term tty -0 ../sources/bitsavers/disks/8550_DIAG.IMD -cycles $((2400*2000000)) -type 31 '\r' -type 41 '1\r' -type 51 '\r'` must end in `SYSTEM VERIFICATION COMPLETE`, with no `DIAGNOSTIC FAILURE`. Repeat with `062-5412-08_8501_1err_DOS50.imd` (version 2.4). With the Z80 emulator board fitted (the default), the only allowed failure is **07/00D0**: its firmware PROM was never dumped. The board's own test is Select mode → 9 (Z80 emulator test).
3. **RTPA:** `build/tek8550 -rtpa -pm ext -term tty -0 ../sources/bitsavers/disks/8550_DIAG.IMD -cycles $((200*2000000)) -type 31 '2\r' -type 41 '4\r' -type 51 '0\r' -type 56 '3\r' -type 61 '\r'` must show no `Error Code` (RTPA.TST option 0, all event and counter tests; option 6 for the trace buffer; option 7 with `-rtpa probe` and an extra `-type 66 '\r'`). ASV with `-rtpa` must complete.
4. **Host link:** `tools/test_host_link.sh` must report 7 passed: DOS/50 COMM transfers against `tools/host_link.py`.
5. **TTA (while in progress):** the same diagnostic command with `-tta` and `-type 41 '6\r' -type 51 '\r' -type 56 '3\r' -type 61 '\r' -type 66 '\r'` (Select mode 6, all modules). Error codes 11/0000–11/0023 and 11/0040–11/0043 must not appear. Without `-tta`, nothing may change.
6. **Floppy:** `make test` (track build/decode round trip, all four geometries). After controller changes, also FORMAT a blank in drive 1 after boot (`-mount 30 1=blank.img -type 34 'FORMAT /DEV/FLX1\r' -type 40 'Y\r'`) and check it with `../tekmato/tekmato -open blank.img -check`; `-fdc hle` must stay byte-identical to the shim.
7. **Z80 core:** zexdoc and zexall must stay cycle-exact (46734978649 cycles). The upstream harness `z80_tests.c` is not in the repo; port callbacks take `uint16_t`.

## Conventions
- **Minimal edits to ported cores:** keep every change small and mark it `[tek8550]`. Record core modifications in `SOURCE.txt` next to the core.
- Machine code mirrors MAME devices (one class per board) so it can be upstreamed later.
- Write new hardware facts into `notes/*.md`, and say how each was established (manual section, ROM address, diagnostic test). Update `PLAN.md`'s status log at milestones.
- **Two program-memory board generations:** the default emulates the boards without the extended comparator, which DOS/50 V1.0 needs. `-pm ext` emulates the newer boards, which the 1983/85 diagnostics need. Don't "fix" one by breaking the other.
- **Active-low registers are everywhere** on the Emulator Controller (F2 pending, F4 bank, F8 debug enables, FA/FB/F5 jump address, FC–FF breakpoints). Check `notes/8301.md` before assuming polarity.
- **Untrusted material:** downloaded files and extracted archives go in their own new directory. Run Python that reads them with `python3 -I`. Original images in `sources/` are never written: they are read-only files, and `-rw` (which writes back every mounted drive) skips any path under `sources/`. Copy an image before writing to it.
- **Prototype link (`notes/prototype-link.md`):** protocol v3. Keep it historically faithful: only real Z80 pin information (address bus, M1, INT/NMI, RESET, WAIT, target power) plus the emulation-only `T` time sync. Remain compatible with v1/v2 targets. FUSE (another session) implements the target side.
- **Ported devices:** generated by a script from `reference/mame` where possible (`emu/tools/port_am9513.py`), so the device code stays verbatim. Hand edits go into the script, not the output.
- **Downloads:** documentation fetched from bitsavers goes in `sources/bitsavers/manuals/<topic>/`, with `pdftotext -layout` text next to it. Make the PDFs read-only.
- **Commits:** local only, no remote. Commit when asked. The user has authorised a local commit at each finished, regression-checked milestone.

## Known gaps
**Not emulated:**
- most of the TTA (Trigger Trace Analyzer, `-tta`): ports, RAM window and the Am9513 work, and TTA.TST modules 00–23 and 40–43 pass; events, triggers, counting, interrupt and acquisition are being reverse-engineered (`notes/tta.md`). RTPA INT 30/31 (unused by DOS/50 V1.0 and RTPA.TST), and RTPA probe inputs other than the diagnostic's loopback (`-rtpa probe`);
- the 6800, 8080 and 8086 emulator boards (an EMU.8086 disc exists);
- target-system modes 1/2 for the 8085 board (the link carries Z80 pins only);
- the 8301 AUX port and the 8501 printer/aux port beyond logging.

**High-level, with known limits:**
- **Board firmware:** the Z80 and 8085 boards' firmware was never dumped. Their tests fail only the PROM checksums 07/00D0 and 06/00D0.
- **PROM programmer:** its controller is simulated, and only the 2708/2716/2732 module exists. Under `-prom fix`, a read after a write in one session hangs, because the PROM software V2.1 expects a later DOS/50.
- **Floppy controller:** the real board runs its firmware, but the drive is a byte-cell model (no flux, weak bits or speed variation; one density per track). Deleted-data (control field) marks are lost when an image is saved. An unformatted disc mounted at power-up makes the 8301 boot time out (as it would on the real lab): mount blanks after booting. A few board details are inferred (`notes/8501-fdc.md`).

**Unknown:**
- Z80 board: E3 bits 4/1 and register-block byte 26;
- 8085 register block: bytes 8 and 13;
- DOS/50 label byte 0x1C.

**Other:**
- Target-system interrupts are only lightly tested.
- Mode 2 runs slowly: one socket round trip per access.
- **Cosmetic:** the CT8500 character ROM is unknown. The screenshot font is the Tek 308 ROM for upper case plus a hand-drawn lower case.
