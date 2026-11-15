# Roadmap

What is left, roughly in order of value. Done work is in [CHANGELOG.md](CHANGELOG.md); register-level findings are in [notes/](notes/).

## Hardware not yet emulated
- **Trigger Trace Analyzer (TTA):** register level in test and run mode (`notes/tta.md`, `notes/tta-os40.md`); the command set is the emulator's stand-in for the lost later DOS/50, programming the board as OS/40 did. Open: port 74 and a few unused bits, and the probe fixture's polarity, which need the service manual's Probe Interface Test section.
- **TTA Data Acquisition Interface:** the probe's eight leads, the EVENT QUALIFIER input and the TRIG OUT / ACQ CLK OUT outputs as a separate connection (the real P6451 was a separate cable), so a target such as FUSE can feed the probe. Needs a small protocol agreed with target authors.
- **The 6800 and 8080 emulator processor boards.** Their firmware does not survive.
- **80186/80188 emulator, the rest** (`notes/8086.md`): and option 8 (MICROLAB modes 1/2), which needs a MicroLab I trainer and its ROM, not preserved.
- **8086/8088 emulator, the rest** (`notes/8086.md`): prototype modes 1/2, the 8087, mapping RAM bits 1/3 and wait states. Its software is DOS/50 V2's (the EMU.8086 disc), which is lost. The 8540's OS/40 support in `roms/emulator_processor/8088/` (2650 code) drives the same board and can be read for the command protocol.
- **8085 target modes 1 and 2:** the prototype link carries Z80 pins only.

## Fidelity
- **Floppies:** the controller runs its real firmware, but the drive is a byte-cell model. IMD discs now round-trip track by track (deleted marks, CRC errors, layouts). Open: flux-level media (weak bits, speed variation, mixed density within a track), which needs a flux image format (HFE, SCP) first; no 8501 disc is known to need it.
- **Undumped firmware** is emulated at high level: the Z80 and 8085 emulator boards (their PROM checksum tests 07/00D0 and 06/00D0 fail) and the PROM programmer's controller. A dump would replace each with the real thing; see "Wanted" in the [README](README.md#wanted).
- **Unknown bits:** Z80 board E3 write bit 2 (named DEBUG by Millennium's 9508 source, for the 8002's boards; bit 4 is INTEN, modelled since 2026-11-15); 8085 register block byte 8 (probably firmware scratch). Both need the boards' firmware PROMs or schematics (`notes/8301.md`).
- **CT8500:** the character ROM was never dumped, monitor mode (control characters shown as symbols) is not modelled, and the learn-status display is an approximation.

## To verify: findings from the 8002's diagnostics
The sibling 8002A emulator runs Millennium's 8002 diagnostics (DIP 4.4; `../tektronix-8002-emulator/notes/diagnostics.md`). What they established was checked against the 8550 on 2026-11-15; the details are in `notes/8301.md`, "Checked against the 8002's diagnostics", and `notes/rtpa.md`.
- ~~**2650 core**~~: RRL and RRR change OVF only when WC = 1 (`[tek8550]`, `emu/src/cpu/s2650/SOURCE.txt`). Still a candidate fix for MAME.
- ~~**`machine/mc6850.h`**~~: taken over from the 8002 (modem lines, break, character time, the data sheet's overrun). Inert here: the host link, the screenshots and both system verifications are unchanged, and no 8550 diagnostic exercises the 8301 ACIAs' modem lines (the I/O test passes with or without wrap-backs).
- ~~**Z80 reset and HALT**~~: a forced RESET keeps the registers but PC, I, R and the interrupt state; PC-next after a HALT is the HALT's address. Z80.TST, LP.TST and system verification pass as before.
- ~~**Emulator Controller and debug rules**~~: PC-last following every instruction, one interrupt per instruction in priority order, and a reset cancelling an armed jump were already modelled and match the 8301 manual. The firmware's debug SVCs now count only while SVCs are enabled.
- **Debug interrupt mask** (open, untestable): the 8002's mask holds only breakpoints and single cycle; the 8301 manual names the bit "Mask debug interrupts", and nothing on the 8550 writes F9 with it low. The model masks every debug interrupt.
- ~~**Z80 board, NMI with a forced jump**~~ and ~~**firmware facts**~~: consistent (the NMI dropped on a jump into the firmware, SP untouched). The 8550 Z80 Specifics confirm the forced jump is a bus sequence ("one wait state ... during a forced jump sequence"); the model still loads the PC directly.
- ~~**The control latch, named**~~: E3 bit 4 is INTEN (the prototype's INT gated by it, inferred, invisible to DOS/50); bit 2 (DEBUG on the 8002) cannot gate breakpoints here, since DOS/50 runs them with it clear.
- ~~**RTPA**~~: status bit 08 at a break is needed by RTPA.TST on the 8550 (the 8002's diagnostics expect it clear); the Z80s now give the RTPA their bus-cycle lengths (no wait states at 4 MHz, 070-3964).
- ~~**A lead for the 8550's own Z80 firmware**~~: not there. The code at `8550_DIAG.IMD` offset 6B460 is Z80.TST's own program, and the diagnostic checks the PROM by a checksum only.
- ~~**Inferred control bits**~~: no gate is inferred on the 8550's boards (E3 bit 2 included).

## Front ends and platforms
- **Windows:** the builds compile and link; they still need a run on a real Windows machine.
- **Target-system interrupts** are only lightly tested, and mode 2 makes one socket round trip per access.

## Project
- **CI:** a GitHub Actions build (Linux, macOS, MinGW cross) running `make test` and the quick regressions (screenshots, host link).
- **Release builds:** tagged binaries for macOS, Linux and Windows, with the GUI.
- **Disc catalogue:** a list of the surviving DOS/50 and diagnostic disc images, what is on each, and which work in the emulator.

## Upstreaming
- Offer the machines to **MAME**: they are structured as MAME devices, and MAME accepts GPL-2.0-or-later contributions. The front panels are already MAME `.lay` artwork.
