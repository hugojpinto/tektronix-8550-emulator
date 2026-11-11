# Roadmap

What is left, roughly in order of value. Done work is in [CHANGELOG.md](CHANGELOG.md); register-level findings are in [notes/](notes/).

## Hardware not yet emulated
- **Trigger Trace Analyzer (TTA):** usable (`docs/TTA.md`): run mode is functional, and the command set is the emulator's stand-in for the lost later DOS/50. Open: run mode at register level (which input group addresses which comparator page, the range codes) and the probe fixture's polarity, which need the original software or its manual sections (`notes/tta.md`).
- **Extended Tekhex** (070-4478-00, Extended Hex Interface Users Manual, on bitsavers): symbol-carrying transfers for the host link (`tools/host_link.py`). 
- **The 6800, 8080 and 8086 emulator processor boards.** An EMU.8086 disc survives; the boards' firmware does not.
- **8085 target modes 1 and 2:** the prototype link carries Z80 pins only.

## Fidelity
- **Floppies:** the controller runs its real firmware, but the drive is a byte-cell model. IMD discs now round-trip track by track (deleted marks, CRC errors, layouts). Open: flux-level media (weak bits, speed variation, mixed density within a track), which needs a flux image format (HFE, SCP) first; no 8501 disc is known to need it.
- **Undumped firmware** is emulated at high level: the Z80 and 8085 emulator boards (their PROM checksum tests 07/00D0 and 06/00D0 fail) and the PROM programmer's controller. A dump would replace each with the real thing; see "Wanted" in the [README](README.md#wanted).
- **Unknown bits:** Z80 board E3 bits 4/1 and register-block byte 26; 8085 register block bytes 8 and 13; DOS/50 label byte 0x1C.
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
