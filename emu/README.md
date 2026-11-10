# Tektronix 8550 emulator (work in progress)

Build: `make` (C++17, no other dependencies). Produces:
- `build/dasm pdp11|s2650|z80 FILE [base] [start] [count]`: disassembler (addresses: hex, or `0o`-prefixed octal)
- `build/tek8501`: the 8501 DMU stand-alone; its AUX port is your terminal

```
build/tek8501 -0 ../sources/bitsavers/disks/System.imd
```
Options: `-roms DIR` (default `../roms/8550/8501`), `-0..-3 DISK` (.imd or raw), `-odt` (start halted in ODT),
`-trace FILE` (instruction trace), `-log FILE` (AUX/printer output), `-cycles N` (batch run, then print registers).
In a terminal, **Ctrl-]** opens the monitor (`h` for help: regs, dump, disasm, step, halt to ODT, restart).
When the CPU halts, the AUX port acts as the LSI-11 ODT console (`addr/`, `Rn/`, CR, LF, `addrG`, `P`).

CPU cores are ported from MAME (BSD-3-Clause, see `../reference/mame/SOURCE.txt`); local changes are marked `[tek8550]`.
