# Tektronix 8550 MDL — emulation plan

Goal: boot DOS/50 on an emulated 8550 (8301 + 8501 + terminal) in a standalone, MAME-compatible emulator and run its editor and assemblers.
Background: `FEASIBILITY.md`. ROMs: `roms/` (with `roms/MANIFEST.md`). Disks: `sources/bitsavers/disks/`. Manuals and their OCR text: `sources/bitsavers/manuals/`.

## Layout
- `emu/`: **standalone C++17 emulator** (decision 2026-11-10: no MAME build, since only about 11 GB of disk is free).
  - CPU cores are ported from MAME's BSD-3 sources (`reference/mame/`), changed as little as possible. Local changes are marked `[tek8550]`.
  - Machines are structured one class per board, mirroring MAME devices so they can be upstreamed later.
- `tools/`: offline helpers (IMD reader, DOS/50 filesystem lister, disassembly scripts).
- `notes/`: hardware notes distilled from the manuals (memory maps, ports, protocols) and disassembly findings.
- Build: `cd emu && make`. It takes seconds and needs no dependencies beyond a C++17 compiler.

## Phases and milestones

### P0 — Groundwork (offline)
- [x] P0.1 Hardware notes for the 8501: memory map, Tek Q-bus CSRs (console/AUX 177560, printer 177510, HSI 176510, floppy 177160, LTC 177546, PROM 173000–174776), interrupt vectors, utility-board LEDs/switches.
- [x] P0.2 Disassemble the 8501 utility ROM (PDP-11, 160-0399/0400 interleaved) and list every device register it touches.
- [x] P0.3 Floppy-controller command protocol (LSI-11 ↔ Z80 command block via 177160), from the manual + the ROM + `8501.SAV`. Decides HLE.
- [x] P0.4 DOS/50 (8501) filesystem: decoded and implemented in the sibling tool `../tekmato` (format with evidence in `../tekmato/FORMAT.md`): list, extract, write, link, check; verified by DOS/50 itself in the emulator.
- [x] P0.5 8301 notes: port map (Tables 7-2, 9-3, 12-2), interrupt vectors, bank switching, memory map. Disassemble the 2650 boot ROM.

### P1 — Toolchain
- [x] P1.1 `emu/` skeleton: emu.h shim, ported disassemblers (PDP-11, 2650, Z80) → `build/dasm`; LSI-11 core port.

### P2 — 8501 stand-alone
- [x] P2.1 LSI-11 + 32 KW RAM + utility PROMs + console ACIA/USART + LTC → **power-up tests print/LED, ODT or debug prompt on the console**.
- [x] P2.2 Add ODT/HALT handling to MAME's `lsi11` if the firmware needs it.
- [x] P2.3 HLE floppy controller backed by the IMD images → **ROM boots `8501.SAV` from the system disk**.
- [x] P2.4 HSI port (176510) as a device with a stub peer.

### P3 — 8301 stand-alone
- [x] P3.1 2650 + System RAM + boot/diag ROMs + bank switch + 3 ACIAs + interval timer → **diag ROM 160-0802-01 runs and reports on the terminal**.
- [x] P3.2 8257 DMA + 8251 HSI.

### P4 — The 8550
- [x] P4.0 Language Processor (Z80 on program memory) + minimal Emulator Controller (F2 pending register, "emulator halted" interrupt, F9 debug command) → get past DOS/50's LP probe to the command prompt.
- [x] P4.1 Join 8301 and 8501 over HSI → **BOOT V2.1 loads `8301/RESIDENT` (diag disk)**, then DOS/50 from the system disk → **DOS/50 prompt**.
- [x] P4.2 Language Processor (Z80) + program memory → run ASM/LINK. (EDIT and ASM verified; LINK not yet tried)

### P5 — Extras
- [x] CT8500 terminal behaviour (for ACE); Z80 and 8085 emulator boards; PROM programmer; memory map and prototype link (v3).
- [x] Host link (COMM/HOST); all four disc geometries and FORMAT; RTPA.
- [ ] TTA: stage 1 done (ports, RAM window, Am9513; TTA.TST 00–23 and 40–43). Next: comparators, counting sources, interrupt, acquisition (`notes/tta.md`).
- [ ] 8086 / 6800 / 8080 emulator boards; 8085 target modes; upstreaming to MAME.

## Status log
- 2026-11-11: **Graphical CT8500 (SDL3).** `make gui` builds `tek8550-gui`: the CT8500 raster (6×8 in 8×10, 640×250, green phosphor, CRT or sharp), typed into directly, with paste at the line rate, copy, turbo, full screen, bell. The terminal model is split from its ANSI renderer (tty output A/B byte-identical through a pty); the raster lives in `host/ct8500_raster.*` with no graphics dependency. `-shot` captures the window; the dummy SDL driver runs it headless. The plain build stays dependency-free.
- 2026-11-11: **Floppy controller, low level.** The board's firmware is ROM 160-0412-00 (A50U2030, already in the set; Z80A + 1 KB RAM + TTL, no FDC chip). Its register map, the address-encoded write controls (mark clocks, CRC preset/insert) and the WAIT-paced byte interface were read out of the firmware (`notes/8501-fdc.md`, `notes/dasm/8501_fdc.lst`). The Z80 now runs it against Qume drive models with byte-cell tracks built from the images. DOS/50 boots (prompt at ~27 s instead of ~24 s: real seeks and rotation), both diagnostic discs pass, FORMAT works for DSDD/DSSD/SSDD, and FORMAT → VERIFY → COPY:L:B duplicates a system disc exactly. `-fdc hle` keeps the shim byte-identical. Runtime disc changes: `-mount`, `-eject`, monitor `m`/`e`.
- 2026-11-11: **System layer.** `emu/src/system/tek8550.*` holds the whole lab with no wall-clock notion; `main8550.cpp` is the tty front end; lamps are named outputs (`-outputs FILE`), restarts and the mode switch named inputs. A pure refactor, A/B byte-identical. Design for the GUI and FDC work: `docs/DESIGN-gui-and-fdc.md`.
- 2026-11-11: **TTA, stage 1.** Found the Trigger Trace Analyzer's interface from TTA.TST V1.3A:
  - ports 70–74;
  - an AMD Am9513 at 76/77, ported from MAME by a generator script;
  - a RAM window at system-bus E800–F9FF that inhibits system RAM.

  Modules 00–23 and 40–43 pass. The TTA service and users manuals were fetched from bitsavers (no register detail). Also: RTPA option 7 passes with `-rtpa probe`, BIF FRZ and pass/delay with ARM/LIM work, and INT 30/31 are unused.
- 2026-11-11: **RTPA.** The Real-Time Prototype Analyzer is emulated (`-rtpa`), reverse-engineered from RTPA.TST's table-driven tests and DOS/50's command overlays:
  - two comparators, two 8253s, pass/delay/arm/limit sequencing;
  - a 128-entry trace with a run tag;
  - PC latches and INT 29.

  RTPA.TST options 0–6 pass, and DOS/50 EVT/BIF/CNT/RTT/DRT work. The trace exposed the Z80 core reading 16-bit operands high byte first (fixed).
- 2026-11-11: **Host link.** The 8301 REMOTE port is connected (`-remote`, `serial_link`), with a reference host `tools/host_link.py`. DOS/50 COMM V4.0 does all four general-host transfers: Tekhex download and upload, and ASCII download and upload of files. Two emulated labs exchange a Tekhex file with HOST/COMM. Findings (`notes/host-link.md`): COMM discards the host's echo of each line it sends, including its ACKs; and HOST needs human typing speed.
- 2026-11-11: **DOS/50 file system + tekmato.** Decoded the 8501 file system from the disc collection and from image diffs around DOS/50 commands in the emulator. The new sibling tool `../tekmato` (tomato-style CLI) reads and writes it, and DOS/50 reads, writes, creates and deletes on volumes it makes. Findings:
  - DOS/50's calendar has twelve 31-day months;
  - "Error 12 Device I/O error on write" is V1.0's protection violation (no PW on a directory you don't own);
  - the SYSTEM disc carries 88 orphaned descriptors.
  Prototype link v3 (WAIT, RESET, target power) was agreed with the FUSE target session and implemented.
- 2026-11-11: **Memory map + target link.** 8301 map/write-protect RAM (MAP PRW/PRO/URW/URO; write violation → `<BREAK MEMWR>`). Board firmware GET/PUT (F803/F800) for prototype memory. Emulation modes 1 and 2 route to an external target over a socket protocol (`notes/prototype-link.md`); the reference target `tools/proto_target.py` prints the output of a program assembled on the 8550 (mode 1: code in program memory; mode 2: code in target RAM).
- 2026-11-11: Both diagnostic disks (2.4 and 3.0) pass Automatic System Verification with zero errors (`-pm ext`). Screenshots are in docs/screenshots/diagnostics.
- 2026-11-11: Z80 emulator processor board (mode 0) and CT8500 terminal/ACE done; full diagnostic suite passes.
- 2026-11-10: **DOS/50 is usable.** Z80 Language Processor (superzazu/z80, MIT, verified with zexall) and the Emulator Controller subset are in. DOS/50 passes its LP probe and gives the `>` prompt. LDIR/USER/DATE/HELP work, the line editor (EDIT 4.0) creates a file, and the **Z80 assembler (ASM V4.0B) assembles it with a correct listing**. Bug found on the way: SVC n is OUT (F8h−n), i.e. the numbering is reversed.
- 2026-11-10: **8301 + full system.** The 8301 diag ROM passes all power-up tests (needed parity-error modelling and per-instruction IRQ sampling). The boot ROM prints "BOOT V2.1". `tek8550` joins both machines over the HSI (byte-level NCTS/RFD flow control, 100 µs lockstep). The real 8301 boot ROM and 8501 OS complete the file-server protocol, load `/VOL/SYSTEM/8301/RESIDENT`, and the terminal shows **"DOS/50 V1.0   18-AUG-80"**. DOS/50 then probes the Language Processor (a Z80 HALT test), which is not emulated yet.
- 2026-11-10: **8501 milestone.** Power-up diagnostics pass after calibrating the CPU budget to 4.5 M T11-cycles/s against the LTC test window. With no disk, the bootstrap sits at "waiting for a disc" (LEDs 30). With `System.imd` it boots DOS/50's 8501 side (LEDs "OS running") into its idle/scheduler loop, presumably waiting for the 8301 on HSI. ODT works.
- 2026-11-10: plan written; MAME clone started.
