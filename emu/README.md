# Tektronix 8550 emulator (work in progress)

Build: `make` (C++17, no other dependencies). Produces:
- `build/dasm pdp11|s2650|z80 FILE [base] [start] [count]`: disassembler (addresses: hex, or `0o`-prefixed octal)
- `build/tek8501`: the 8501 DMU stand-alone; its AUX port is your terminal
- `build/tek8301`: the 8301 MDU stand-alone; its system-terminal port is your terminal (runs the power-up tests, then "BOOT V2.1")
- `build/tek8550`: the full 8550, with the 8301 and 8501 joined by the HSI link; your terminal is the 8301 system terminal

```
build/tek8550 -0 ../sources/bitsavers/disks/System.imd      # boots DOS/50 to its > prompt
build/tek8501 -0 ../sources/bitsavers/disks/System.imd
```
Leaving: **Ctrl-]** opens the monitor; `q` + Enter quits (disks mounted with `-rw` are saved), `c` or Enter resumes.

`-rw` writes disk changes back to the image (raw `.img` or `.imd`; atomic, on quiet periods and at exit).
`-pm ext` emulates the 1981+ program-memory boards with the extended-bank comparator. The 1983 diagnostic disk needs it;
DOS/50 V1.0 needs the default (no comparator).

Hardware self-test (all boards):
```
build/tek8550 -pm ext -0 ../sources/bitsavers/disks/062-5412-08_8501_1err_DOS50.imd   # <CR>, 1<CR>, <CR> -> Automatic System Verification
```

tek8550 extras: `-hsilog FILE` (hex log of the HSI traffic), `-auxlog FILE` (8501 AUX port), `-dump FILE` (memory images),
`-type SECONDS TEXT` (type into the terminal at a machine time; `\r` Return, `\e` ESC, `\xHH`), `-trace8301 T FILE`, `-trace8501 T LEN FILE`. Env `TEK_IOTRACE=1` / `TEK_IOTRACE8301=1` trace device I/O.
Options: `-roms DIR` (default `../roms/8550/8501`), `-0..-3 DISK` (.imd or raw), `-odt` (start halted in ODT),
`-trace FILE` (instruction trace), `-log FILE` (AUX/printer output), `-cycles N` (batch run, then print registers).
In a terminal, **Ctrl-]** opens the monitor (`h` for help: regs, dump, disasm, step, halt to ODT, restart).
When the CPU halts, the AUX port acts as the LSI-11 ODT console (`addr/`, `Rn/`, CR, LF, `addrG`, `P`).

CPU cores are ported from MAME (BSD-3-Clause, see `../reference/mame/SOURCE.txt`); local changes are marked `[tek8550]`.

## Example session (works)
```
> SEL Z80
> EDIT T.ASM          ** EDIT VERSION 4.0 / ** NEW FILE
*INPUT                (type lines, empty line ends input)
*FILE
> ASM T.OBJ T.LST T.ASM      Tektronix Z80 ASM V4.0B ... No assembly errors detected
> COPY T.LST CONO
```
Disk writes stay in memory (the image file is not modified).
