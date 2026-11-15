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
- **Unknown bits:** Z80 board E3 write bits 4 and 2; 8085 register block byte 8 (probably firmware scratch). Both need the boards' firmware PROMs or schematics (`notes/8301.md`).
- **CT8500:** the character ROM was never dumped, monitor mode (control characters shown as symbols) is not modelled, and the learn-status display is an approximation.

## To verify: findings from the 8002's diagnostics
The sibling 8002A emulator now runs Millennium's 8002 diagnostics (DIP 4.4, 2026-11-15; `../tektronix-8002-emulator/notes/diagnostics.md`). Some of what they established touches code or assumptions the two projects share; the 8002's copies of the shared files now differ from these.
- ~~**2650 core**~~ (done 2026-11-15): RRL and RRR change OVF only when WC = 1, as the 8002's MASTER CPU diagnostic checks on real silicon (MAME, following the 1975 Signetics manual's note, changes it on every rotate that flips bit 7). Ported as `[tek8550]` (`emu/src/cpu/s2650/SOURCE.txt`); the regression list passes unchanged. Still a candidate fix for MAME.
- **`machine/mc6850.h`** (shared): the 8002's copy adds RTS/CTS/DCD, break, character time from the control register, and an overrun that shows only after the previous character is read (MC6850 data sheet; the SYS COMM test with its wrap-back fixture). All inert unless driven, so taking the file over should change nothing here; check which 8301/8501 ports have modem lines wired, and whether the 8550 diagnostics' serial tests could then run with a `loop` plug driving them.
- **Emulator Controller and debug rules**: on the 8002's Debug module the diagnostics showed that PC last/next follow every instruction while debugging (not only the stops); that breakpoint 1, breakpoint 2 and single step on one instruction raise one interrupt, in that priority; that a reset or interrupt commanded without a forced jump cancels a jump a stop had armed; that SVCs count only while enabled, the board firmware's own included; and that a board parks after an SVC only if its firmware made it. Check which of these the 8301's Emulator Controller shares, against its diagnostics.
- **Z80 board, NMI with a forced jump**: DOS/50, like TEKDOS, reaches a halted Z80 with an NMI plus a forced jump. The NMI pushes the PC on the user's stack, and whether the (lost) firmware puts SP back is unknown. The 8002 model drops the NMI when a jump comes with it, leaving SP alone; keep the two consistent.
- **Z80 board firmware facts** (the 8002's board firmware, rebuilt 2026-11-15 from a copy in Millennium's diagnostics; `../tektronix-8002-emulator/notes/emulators.md`, "The Z80 board's firmware"): the forced jump is a JP jammed onto the bus (an M1 cycle; the firmware's R compensation counts it), a halted Z80 ignores it (hence NMI + jump, the NMI taken first), and the dump adds a block byte (1A on the 8002) to SP to take back an earlier NMI's push. DOS/50's board is a different firmware version, but the board logic is likely shared: check the 8550 model's forced jump, halt and SP handling against these.
- **A lead for the 8550's own Z80 firmware:** `8550_DIAG.IMD` holds Z80 code at image offset 6B460 (a jump table, then a dump routine: `LD (0028),SP; LD SP,0028`, pushes, the alternate set, I). Its addresses (around 0100) and larger block suggest the diagnostic's own Z80 test kernel rather than a copy of the board's PROM, but it may show how the board is driven; the 8002's firmware turned up inside a diagnostic's PROM test, so the 8550 diagnostics' Z80 module (and its 07/00D0 PROM static test) are worth reading for a table the same way.
- **Inferred control bits**: the 8002's "board signals reach the debug logic" bit, inferred from TEKDOS, turned out to be wrong for its 6800 and 8080 boards. Any such inferred gate on the 8550's boards (E3 bit 2 is still unknown, `notes/8301.md`) should be checked against the 8550 diagnostics.

## Front ends and platforms
- **Windows:** the builds compile and link; they still need a run on a real Windows machine.
- **Target-system interrupts** are only lightly tested, and mode 2 makes one socket round trip per access.

## Project
- **CI:** a GitHub Actions build (Linux, macOS, MinGW cross) running `make test` and the quick regressions (screenshots, host link).
- **Release builds:** tagged binaries for macOS, Linux and Windows, with the GUI.
- **Disc catalogue:** a list of the surviving DOS/50 and diagnostic disc images, what is on each, and which work in the emulator.

## Upstreaming
- Offer the machines to **MAME**: they are structured as MAME devices, and MAME accepts GPL-2.0-or-later contributions. The front panels are already MAME `.lay` artwork.
