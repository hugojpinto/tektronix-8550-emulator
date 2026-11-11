# Roadmap

What is left, roughly in order of value. Done work is in [CHANGELOG.md](CHANGELOG.md); register-level findings are in [notes/](notes/).

## Hardware not yet emulated
- **Trigger Trace Analyzer (TTA):** stage 1 is in (ports, RAM window, Am9513; TTA.TST modules 00–23 and 40–43 pass). Next are the event comparators (modules 30/31), counting from live sources (44–46), interrupt (50), acquisition (60) and the probe. Start from `notes/tta.md`. No software on hand uses it on the 8550, so the diagnostic is the only consumer.
- **The 6800, 8080 and 8086 emulator processor boards.** An EMU.8086 disc survives; the boards' firmware does not.
- **8085 target modes 1 and 2:** the prototype link carries Z80 pins only.

## Fidelity
- **Floppies:** the controller runs its real firmware, but the drive is a byte-cell model. Open: flux-level media (weak bits, speed variation), mixed density within a track, and keeping deleted-data (control field) marks in saved images.
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
