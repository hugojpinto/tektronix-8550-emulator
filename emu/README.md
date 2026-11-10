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

## Terminal: CT8500 emulation (default when interactive)
Your terminal shows an emulated **Tektronix CT8500** (25×80 window over 50 lines of memory, attributes, split screen,
cursor/top-line queries), so the full-screen **ACE** editor works. Use a terminal of at least 80×25. `-term tty` gives raw
pass-through instead, and `-screen FILE` dumps the emulated screen at exit.

Key map (ACE's configurable commands in brackets, see ACE manual Fig. 4-2):

| Your key | CT8500 key | ACE |
|---|---|---|
| arrows / Home | cursor keys / HOME | (cu) (cd) (cl) (cr) (ch) |
| F1 F2 F3 F4 | F1–F4 (SCROLL ← ↓ ↑ →) | (sl) (sd) (su) (sr) |
| F5 / PgDn, F6 / PgUp | F5 PAGE↓, F6 PAGE↑ | (pd) (pu) |
| F7 | F7 REVISE | (rm) |
| F8 | F8 | |
| Insert / Delete | INSERT / DELETE CHAR | (ic) (dc) |
| F9 / F10 | INSERT / DELETE LINE | (il) (dl) |
| F11 / F12 | ERASE to EOL / EOP | |
| Shift-Tab | BACK TAB | (mark) |
| Ctrl-J | LINE FEED | (cmd-esc) |
| Esc | ESC | ends insert; `EX` saves & exits ACE |

## Running and debugging Z80 programs (emulation mode 0)
The Z80 emulator processor board is emulated (its firmware at high level, see `notes/8301.md`):
```
> SEL Z80
> ASM P.OBJ P.LST P.ASM
> LINK P.LOAD P.LNKL P.OBJ
> LOAD P.LOAD
> BK 106            breakpoint
> TRACE ALL         single-step display (TRACE OFF to stop)
> GO                run; ESC ESC breaks a free-running program
> DS                display registers;  D 1000 100F  dump memory
```
