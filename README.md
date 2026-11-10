# Tektronix 8550 Microcomputer Development Lab — emulator

An emulator for the **Tektronix 8550 MDL** (1980), a rare microprocessor development system. It emulates:
- the **8301 MDU**: 2650 system processor, Z80 Language Processor, Emulator Controller and Z80 emulator board;
- the **8501 DMU**: LSI-11/2 file server with 8" floppies;
- the **CT8500 terminal**.

It boots the original firmware and **DOS/50 V1.0** from the surviving disk images, runs Tektronix's own editors, assembler, linker and debugger, and passes the factory diagnostic suites.

![DOS/50 booting](docs/screenshots/01_boot.png)
![ACE full-screen editor](docs/screenshots/03_ace_edited.png)
![Diagnostics passing](docs/screenshots/diagnostics/d30_4_complete.png)

## What works
- The power-up diagnostics of both units, then `BOOT V2.1` and `DOS/50 V1.0 18-AUG-80`, all from the original ROMs and disks.
- `EDIT`, **ACE** (full screen, through an emulated CT8500), `ASM` (Z80 ASM V4.0B), `LINK`, `HELP`, `LDIR` and the rest of the DOS/50 command set.
- Running and debugging Z80 programs on the emulator board: `GO`, breakpoints, `TRACE`, `DS`, memory display, Esc Esc break, and `MAP` write protection (`<BREAK MEMWR>`).
- **Emulation modes 1 and 2** against an external "prototype" process over a socket. This includes [FUSE](https://fuse-emulator.sourceforge.net/) acting as a ZX Spectrum target.
- **Diagnostic System 2.4 and 3.0:** Automatic System Verification passes with zero errors.
- Disk write-back to raw or IMD images.

## Quick start
```
cd emu && make                                   # C++17, no dependencies
cp ../disks/System_helpfix.img ../disks/work.img
build/tek8550 -rw -0 ../disks/work.img           # wait ~24 s for the > prompt
```
Use a terminal of at least 80×25. To quit, press **Ctrl-]**, then `q`.

## Documentation
| | |
|---|---|
| [docs/OPERATION.md](docs/OPERATION.md) | hands-on tutorial: boot, DOS/50, edit, assemble, link, run, debug, target systems, diagnostics, scripting |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | how the 8550 works and how the emulator models it |
| [emu/README.md](emu/README.md) | emulator options and the CT8500 key map |
| [notes/](notes/) | register-level hardware notes and reverse-engineering findings (8301, 8501, Z80 emulator board, prototype link) |
| [docs/screenshots/](docs/screenshots/) | a full edit → assemble → link → run session, and the diagnostics |
| [PLAN.md](PLAN.md), [FEASIBILITY.md](FEASIBILITY.md) | project history and the original feasibility study |
| [CLAUDE.md](CLAUDE.md) | orientation for contributors and coding agents: layout, conventions, regression checks |

## Repository layout
```
emu/         the emulator (C++17): cpu/ cores, machine/ boards, host/ terminal and IPC
tools/       Python helpers: IMD reader, reference target, screenshot renderer
notes/       hardware notes and ROM disassembly listings
docs/        tutorials and screenshots
roms/        ROM images used by the emulator (see roms/MANIFEST.md)
disks/       working disk images (DOS/50 with a repaired HELP file)
sources/     original material: disk images, manual OCR text, photos (PDF scans are not committed)
reference/   the MAME source files the CPU cores were ported from
```

## Not (yet) emulated
- The other emulator boards (8085, 6800, 8086…).
- The real-time analysers (RTPA, TTA) and the PROM programmer.
- The CT8500's original character ROM. It was never dumped, so screenshots use a Tektronix 308 ROM upper case plus a hand-drawn lower case.
- The Z80 emulator board's firmware. It is not dumped either, and is emulated at high level from reverse engineering.

## Credits and licences
- **CPU cores:** the 2650 and LSI-11 cores, and the Z80 disassembler, are ported from [MAME](https://www.mamedev.org/) (BSD-3-Clause; see `reference/mame/SOURCE.txt`). The Z80 core is [superzazu/z80](https://github.com/superzazu/z80) (MIT). Local changes are marked `[tek8550]`.
- **Sources:** ROM dumps, disk images and manuals come from [bitsavers](https://bitsavers.org/pdf/tektronix/85xx/) and [TekWiki](https://w140.com/tekwiki/wiki/8550), and are © Tektronix. They are included for preservation and research.
- **Emulator code:** the code in `emu/` and `tools/` is new work, structured so it can be upstreamed into MAME.
