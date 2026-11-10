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
- [ ] P0.4 DOS/50 (8501) filesystem: directory format, file lister/extractor, then extract `8501.SAV`, `8301/RESIDENT`, `KERNL` etc.
- [ ] P0.5 8301 notes: port map (Tables 7-2, 9-3, 12-2), interrupt vectors, bank switching, memory map. Disassemble the 2650 boot ROM.

### P1 — Toolchain
- [x] P1.1 `emu/` skeleton: emu.h shim, ported disassemblers (PDP-11, 2650, Z80) → `build/dasm`; LSI-11 core port.

### P2 — 8501 stand-alone
- [x] P2.1 LSI-11 + 32 KW RAM + utility PROMs + console ACIA/USART + LTC → **power-up tests print/LED, ODT or debug prompt on the console**.
- [x] P2.2 Add ODT/HALT handling to MAME's `lsi11` if the firmware needs it.
- [x] P2.3 HLE floppy controller backed by the IMD images → **ROM boots `8501.SAV` from the system disk**.
- [ ] P2.4 HSI port (176510) as a device with a stub peer.

### P3 — 8301 stand-alone
- [ ] P3.1 2650 + System RAM + boot/diag ROMs + bank switch + 3 ACIAs + interval timer → **diag ROM 160-0802-01 runs and reports on the terminal**.
- [ ] P3.2 8257 DMA + 8251 HSI.

### P4 — The 8550
- [ ] P4.1 Join 8301 and 8501 over HSI → **BOOT V2.1 loads `8301/RESIDENT` (diag disk)**, then DOS/50 from the system disk → **DOS/50 prompt**.
- [ ] P4.2 Language Processor (Z80) + program memory → run ASM/LINK.

### P5 — Extras
- CT8500 terminal behaviour (for ACE), emulator-processor boards (8085/Z80), PROM programmer, upstreaming to MAME.

## Status log
- 2026-11-10: **8501 milestone.** Power-up diagnostics pass after calibrating the CPU budget to 4.5 M T11-cycles/s against the LTC test window. With no disk, the bootstrap sits at "waiting for a disc" (LEDs 30). With `System.imd` it boots DOS/50's 8501 side (LEDs "OS running") into its idle/scheduler loop, presumably waiting for the 8301 on HSI. ODT works.
- 2026-11-10: plan written; MAME clone started.
