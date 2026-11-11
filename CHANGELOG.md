# Changelog

What happened, newest first. Open work is in [ROADMAP.md](ROADMAP.md); the study that started the project is [docs/history/FEASIBILITY.md](docs/history/FEASIBILITY.md).

- 2026-11-11: **8086 TTA interface.** The TTA watches the 8086 at register level. The bus status byte's layout comes from the decode table in the 8540's 8088 support ROM; run mode at register level comes from the 8086 diagnostic's TTA test. The diagnostic's module 70 now passes, and with it the whole of option 0, with `-tta`.
- 2026-11-11: **tekmato 0.2.1:** names with DOS/50's abbreviation brackets (`ATT[RIBUTE]`, `DS[]`) can be created; quote them in the shell. DOS/50 itself applies abbreviations only to command lookup, not to paths.
- 2026-11-11: **8086/8088 emulator.** The board set (`-ep 8086`, `-ep 8088`) runs its real kernel. The kernel is a 2 KB 8086 program in two PROMs that had been in `roms/emulator_processor/8088/` all along (160-1312-00 even bytes, 160-1311-00 odd).
  - **Core:** MAME's 8086/8088 core and x86 disassembler are ported (`cpu/i86`, `cpu/i386`). On the way, a MAME bug that sent every segment override to the code space is fixed, and the LOCK pin is modelled for every instruction; the board depends on it. `build/dasm i86` disassembles 8086 code.
  - **The board** (`machine/ep8086.*`; `notes/8086.md`) was worked out from the kernel and the 1983 diagnostic's 8086 test:
    - the 2650 talks to the kernel through a command port and a 32-byte common RAM;
    - the board's breakpoint, mapping and relocation RAMs appear in program memory;
    - LOCK marks the kernel's accesses to the user's side, and a LOCKed IRET starts the program;
    - a break returns through NMI. Breakpoint arming is done by feeding flip-flops back into a breakpoint RAM's address.
  - **Result:** the diagnostic (Select mode 17) passes every module of option 0 and the slow RAM test, for both processors. Module 31's PROM checksums confirm the dump. Still open: the TTA interface (module 70), the prototype modes, and the software, which needs the lost DOS/50 V2.
- 2026-11-11: **TTA checked against TNIX's TTA help** (the 8560's 1983 menu script for the same commands). The Z80/8085 bus symbols are now established rather than chosen. Breaks act on a trigger's occurrence, not on every instruction while it holds, per its "3 events and 1 trigger" rule. Its timing and counting recipes run unchanged and give the hand-computed results.
- 2026-11-11: **TTA on the 8085 and on target systems.** The TTA watches the 8085 emulator board too: the manual's 8085 session reproduces its Display 1-2 exactly, and DOS/50's 8085 support prints `<BREAK TRIG1>` itself. Checked in emulation modes 1 and 2 against `tools/proto_target.py`: target I/O and code running from target RAM are seen and recorded like any other cycle.
- 2026-11-11: **TTA in use.** The TTA now watches the program under test.
  - **Run mode** (`machine/tta_run.*`, functional): events, consecutive events, the four counters with their output modes, sources, gates and restart, breaks (stop/continue) and acquisition.
  - **Commands:** the users manual's command set (`system/tta_console.*`) is typed at the DOS/50 prompt, as on the later DOS/50 that had it.
  - **Tutorial:** `docs/TTA.md` follows the manual's learning guide with its own sample program, which is valid Z80 code. The board internals moved to `docs/ARCHITECTURE.md` §8 and the diagnostic walkthrough to `docs/OPERATION.md` §10.
  - **Checked:** the manual's results reproduce: the break at 0106 with A = 01, five continue lines, and the operand-only recording that exposes the bad table entry. A code-timing measurement gives 39.4 µs against 39.0 µs computed.
- 2026-11-11: **TTA guide and probe test.** `docs/TTA.md` explains the TTA with pictures from `tools/make_tta_shots.sh`: what it does, running TTA.TST, reading its reports, and the board at register level. The probe test's code showed that page 73's high nibble, not pages 76/77, enables the events (page 73 is the probe byte's RAM). With `-tta probe`, the probe clipped onto the extended address lines (inferred), option 7 passes too.
- 2026-11-11: **TTA, test mode.** TTA.TST option 0 now passes in full (modules 00–60, 30 iterations without an error). Found from the diagnostic's code, in `notes/tta.md`:
  - **Events:** in test mode the comparator RAMs are addressed by the 2650's A7–0 and latch four events per access; port 74's 2-bit codes (00 = match) select them, gated by pages 76/77.
  - **Triggers:** event AND counter output, with trigger N arming N+1 via port 71, latched in port 73.
  - **Counters:** the Am9513 runs from 5 MHz, gated by port 70 bit 2 (exactly 2.5 counts per 2650 clock, so I/O is now timed to the instruction). Event k drives SRC k and GATE k+1.
  - **INT 29:** raised by a new trigger enabled in port 72, with its own request flip-flop shown in F2 bit 2.
  - **Acquisition:** into pages 68–6F at port 75's pointer.

  Fixed on the way: three Am9513 port gaps, in its generator. The Emulator Controller's "halted" interrupt is now raised on every new HALT when no slave is enabled. Run mode and the probe interface stay open.
- 2026-11-11: **IMD discs, track by track.** An IMD disc now goes onto the drive as recorded: per-sector ID fields, F8 deleted-data marks, data CRC errors and missing data fields. Saving writes back exactly what is on the media, so nothing is flattened to a linear image any more. `make test` round-trips every IMD in `sources/` plus a synthetic track with every kind of mark; a deleted sector survives a DOS/50 write session. Raw `.img` discs work as before.
- 2026-11-11: **AUX and printer ports.** `-printer`, `-aux8501` and `-aux8301` plug a printer (`file:`), a wrap-back connector (`loop`) or a socket into the remaining RS-232 ports. DOS/50 `PRINT` works: it needs the peer's DTR/RTS in RCSR bits 14/13 and takes the printer's transmitter interrupt at vector 200 (the LAV-11 convention), both found in its code. The diagnostic recognises each peer: a printer gets the Fig. 4-16 pattern, and a wrap-back passes. Its 8301 display option 2 prints the whole log on J103. The 8501 console works: ODT appears on `-aux8501` when the LSI-11 halts, and `P` resumes it. Facts in `notes/8501.md`.
- 2026-11-11: **Operable front-panel switches.** 8301 POWER and 8501 DC switch a unit off (lamps dark, HSI cable dead, power-up with cleared memory when switched back on); 8501 HALT/RUN halts and resumes the LSI-11; RESTART holds a unit in reset while held and starts it on release, with human-like press times (at least 150 ms for a click, 200 ms for hotkeys, the monitor and `-press`). The system clock keeps counting while the 8301 is off or held, so scripts and pacing are unaffected. New `-input`/`-press` options; switch positions are outputs. Verified in scripted runs: an 8301 power cycle and RESTART each reboot DOS/50, which then lists the disc; HALT during an LDIR stops the LSI-11 (PROCESSOR BUSY goes out) and after RUN the listing completes.
- 2026-11-11: **CT8500 keyboard.** Mapped from the Operators Manual: local/power-up/host-control semantics, REMOTE, LOCAL, TTY LOCK, BREAK (a framing-error null on the ACIA), SEND line/page, learn mode with programmable shifted F1–F8 (remote and local) and playback, the CNTRL-SHIFT commands, edge-wrapping cursor keys. PgUp/PgDn fixed (F5 = Page One = ACE's 'page down'). `make test` checks 27 behaviours against the manual.
- 2026-11-11: **Public release.** GPL-2.0-or-later (`LICENSE`, `NOTICE.md`), authorship headers on every original file, personal files out of the history, published on GitHub. `PLAN.md` became this changelog plus `ROADMAP.md`; the feasibility study moved to `docs/history/`.
- 2026-11-11: **Documentation.** README rewritten around a hero picture of the graphical lab; `docs/TUTORIALS.md` with five short recipes and progress screenshots, rendered reproducibly by `tools/make_tutorial_shots.sh` and `tools/make_hero.sh` (GUI batch runs advance a fixed 1/60 s of machine time per frame).
- 2026-11-11: **Windows.** Portable sockets (`host/net.*`, Winsock) and console (`host/tty.h`, VT mode); the Makefile builds static `.exe` files natively under MSYS2 or cross with `CROSS=x86_64-w64-mingw32-`, and the GUI against SDL3's MinGW package. Verified by cross-compiling; not yet run on Windows.
- 2026-11-11: **Front panels after real photos.** The owner's photos of an 8501, an 8540 and a sharper catalogue scan fixed the artwork: AC ON (not STANDBY), the HALT/RUN switch, the 8301's horizontal control panel with a portrait POWER rocker, equal box heights. A design pass aligned both panels on one grid (`tools/gen_panels.py`). The windows fit and centre themselves on the screen.
- 2026-11-11: **System disc restored.** `disks/System_Fixed.img` had an experiment on it; it is rebuilt from the original `System.imd` by `tools/make_system_fixed.py`.
- 2026-11-11: **Front panels.** `tek8550-gui` opens the 8301 and 8501 fronts from MAME-style `.lay` artwork (`emu/artwork/`), drawn after photos of real units (tekwiki brochure shot; the owner's VCFed thread photos: 8501 front, the 8540 whose control strip matches the 8301's). Live lamps: SYSTEM/PROGRAM/DMA/SELF TEST, ON/AC ON/PROCESSOR BUSY, drive activity, internal diagnostic LEDs; RESTART buttons; drag a disc image onto a drive to insert, click to eject. Found on the way: 8301 port ED D5 (SELF TEST) is active low.
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

## The original plan (2026-11-10), completed

Goal: boot DOS/50 on an emulated 8550 (8301 + 8501 + terminal) in a standalone, MAME-compatible emulator and run its editor and assemblers. Every phase below was done; P2.3's high-level floppy controller was later replaced by the real board's firmware, and the open P5 items moved to the roadmap.

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
