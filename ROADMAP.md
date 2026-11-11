# Roadmap

What is left, roughly in order of value. Done work is in [CHANGELOG.md](CHANGELOG.md); register-level findings are in [notes/](notes/).

## Hardware not yet emulated
- **Trigger Trace Analyzer (TTA):** usable (`docs/TTA.md`): run mode is functional, and the command set is the emulator's stand-in for the lost later DOS/50. Open: run mode at register level (which input group addresses which comparator page, the range codes) and the probe fixture's polarity, which need the original software or its manual sections (`notes/tta.md`).
- **TTA Data Acquisition Interface:** the probe's eight leads, the EVENT QUALIFIER input and the TRIG OUT / ACQ CLK OUT outputs as a separate connection (the real P6451 was a separate cable), so a target such as FUSE can feed the probe. Needs a small protocol agreed with target authors.
- **User I/O SVCs under DOS/50 V1.0** (assign, read, write) hang: the request is queued and never sent to the 8501 (`notes/tta.md`). The exit SVC works since 2026-11-11.
- **Extended Tekhex** (070-4478-00, Extended Hex Interface Users Manual, on bitsavers): symbol-carrying transfers for the host link (`tools/host_link.py`). 
- **The 6800 and 8080 emulator processor boards.** Their firmware does not survive.
- **80186/80188 emulator, the rest** (`notes/8086.md`): and option 8 (MICROLAB modes 1/2), which needs a MicroLab I trainer and its ROM, not preserved.
- **8086/8088 emulator, the rest** (`notes/8086.md`): prototype modes 1/2, the 8087, mapping RAM bits 1/3 and wait states. Its software is DOS/50 V2's (the EMU.8086 disc), which is lost. The 8540's OS/40 support in `roms/emulator_processor/8088/` (PDP-11) drives the same board and can be read for the command protocol.
- **8085 target modes 1 and 2:** the prototype link carries Z80 pins only.

## Fidelity
- **Floppies:** the controller runs its real firmware, but the drive is a byte-cell model. IMD discs now round-trip track by track (deleted marks, CRC errors, layouts). Open: flux-level media (weak bits, speed variation, mixed density within a track), which needs a flux image format (HFE, SCP) first; no 8501 disc is known to need it.
- **Undumped firmware** is emulated at high level: the Z80 and 8085 emulator boards (their PROM checksum tests 07/00D0 and 06/00D0 fail) and the PROM programmer's controller. A dump would replace each with the real thing; see "Wanted" in the [README](README.md#wanted).
- **Unknown bits:** Z80 board E3 write bits 4 and 2; 8085 register block byte 8 (probably firmware scratch). Both need the boards' firmware PROMs or schematics (`notes/8301.md`).
- **CT8500:** the character ROM was never dumped, monitor mode (control characters shown as symbols) is not modelled, and the learn-status display is an approximation.

## Front ends and platforms
- **Windows:** the builds compile and link; they still need a run on a real Windows machine.
- **Target-system interrupts** are only lightly tested, and mode 2 makes one socket round trip per access.

## Project
- **CI:** a GitHub Actions build (Linux, macOS, MinGW cross) running `make test` and the quick regressions (screenshots, host link).
- **Release builds:** tagged binaries for macOS, Linux and Windows, with the GUI.
- **Disc catalogue:** a list of the surviving DOS/50 and diagnostic disc images, what is on each, and which work in the emulator.

## Upstreaming
- Offer the machines to **MAME**: they are structured as MAME devices, and MAME accepts GPL-2.0-or-later contributions. The front panels are already MAME `.lay` artwork.
