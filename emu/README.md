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

**Disc formats:** the 8501 drives take all four IBM-compatible 8" formats. An image's size identifies its format:

| Format | Records | Image size |
|---|---|---|
| single-sided, single density (SSSD) | 1001 | 256 256 bytes |
| single-sided, double density (SSDD) | 1989 | 509 184 bytes |
| double-sided, single density (DSSD) | 2002 | 512 512 bytes |
| double-sided, double density (DSDD, the standard 8501 disc) | 3991 | 1 021 696 bytes |

Any other size is treated as DSDD.

**A new disc:**
1. Create a blank image of the right side count: `head -c 1021696 /dev/zero > new.img` for two-sided, or 509184 bytes for one-sided.
2. Mount it with `-rw -1 new.img` and run `FORMAT /DEV/FLX1 [maxfiles] [name]` for double density, or `FORMAT:S` for single density. The image is re-laid out at the formatted density.

`FORMAT:Z` only writes a fresh file system, without formatting the tracks. `../tekmato` can also make, read and write such volumes offline.
`-pm ext` emulates the 1981+ program-memory boards with the extended-bank comparator. The 1983 diagnostic disk needs it;
DOS/50 V1.0 needs the default (no comparator).

Hardware self-test (all boards):
```
build/tek8550 -pm ext -0 ../sources/bitsavers/disks/062-5412-08_8501_1err_DOS50.imd   # <CR>, 1<CR>, <CR> -> Automatic System Verification
# expected: one failure, 07/00D0 (Z80 emulator PROM checksum, PROM not dumped). -ep none: no emulator board -> clean pass.
# -ep-prom FILE: a 512-byte dump of the Z80 emulator board's firmware PROM (F800-F9FF)
```

## 8085 emulator processor board (8300E06)
`-ep 8085` fits the 8085 board instead of the Z80 board. First install DOS/50's 8085 support once: with the 8085 disk in drive 1, run `/VOL/EMU.8085/INSTALL`, using `-rw` on a copy of the system disk. Then `SEL 8085`, and ASM, LINK, LOAD, GO, BK, DS and TRACE work as for the Z80. The diagnostic (Select mode → 7) passes all but 06/00D0, the firmware PROM checksum: that PROM was not dumped.

## PROM programmer (8550F30)
- **Options:**
  - `-prom on` fits the programmer: ports D0/D1, interrupts 13/14, a high-level model of its 8085 controller. With it, PROM.TST (diagnostics, Select mode → 20) passes all 14 options.
  - `-prom fix` also adapts the 1982 PROM software V2.1 to DOS/50 V1.0 (see `notes/8301.md`).
  - `-prom-dev FILE` is the device in the ZIF socket: a raw image, created erased (FF) if missing and saved at exit.
- **Emulated characteristic module:** 2708, 2716 and 2732 EPROMs. Programming only clears bits, as on a UV EPROM.
- **Install the commands once:** with the system disk in drive 0 and the PROM disk in drive 1, run `/VOL/PP/INSTALL1` (use `-rw` on a copy of the system disk).
- **Example:**
```
build/tek8550 -prom fix -prom-dev my2716.bin -rw -0 ../disks/work.img
> RPROM 1000 2716 0 7FF      read the device into program memory 1000-17FF
> CPROM 1000 2716 0 7FF      compare
> WPROM 1000 2716 0 7FF      program it from 1000-17FF
> PTYPE / PSTAT              device types / controller status
```

tek8550 extras: `-hsilog FILE` (hex log of the HSI traffic), `-auxlog FILE` (8501 AUX port), `-dump FILE` (memory images),
`-type SECONDS TEXT` (type into the terminal at a machine time; `\r` Return, `\e` ESC, `\xHH`), `-trace8301 T FILE`, `-trace8501 T LEN FILE`.
`-fdc lle|hle` picks the floppy controller: `lle` (default) runs the real board's Z80 firmware against modelled drives, `hle` the old high-level shim.
`-mount SECONDS N=FILE` inserts a disc at a machine time (the drive reports a disc change), `-eject SECONDS N` writes it back and removes it; the monitor has `m N FILE` and `e N`. Mount blank (empty) images after DOS/50 has booted: an unformatted disc at power-up delays the 8501 so much that the 8301 boot gives up, as on the real lab.
`-outputs FILE` logs the front-panel lamps (`8301:led0..4`, `8301:selftest`, `8301:ramled0..4`, `8501:led0..4`; 1 = lit) with their machine times.
The tek8550 monitor adds `o` (show the lamps) and `p NAME [VALUE]` (inputs: `8301:restart`, `8501:restart`, `8301:modesw`).
The whole system lives in `src/system/` (`tek8550.*`, `options.*`, `outputs.h`); `main8550.cpp` is only the tty front end. Env `TEK_IOTRACE=1` / `TEK_IOTRACE8301=1` trace device I/O.
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

## Memory map, write protection, and a target system (modes 1/2)
- `MAP` / `MAP PRO 1000 10FF` etc.: the 8301 memory-map / write-protect RAM is emulated; a program writing into a
  read-only block stops with `<BREAK MEMWR>`.
- `EM 1` / `EM 2` plus `-target tcp:HOST:PORT` (or `unix:PATH`) connects a user "prototype" process over IPC: U-mapped
  memory, I/O and interrupts go to it. A reference target with RAM and a console UART is `../tools/proto_target.py`.
  Protocol and details: `../notes/prototype-link.md`.
- `-remote tcp:HOST:PORT | listen:PORT | unix:PATH` connects the 8301's REMOTE port (DOS/50 `COMM` and `HOST`) to a host program. `-remote-baud N` sets the REMOTE BAUD switch (default 2400). `tools/host_link.py` is a reference host with Tekhex and ASCII transfers. Two `tek8550`s can also be linked, one running HOST and the other COMM. See `notes/host-link.md`.
- `-rtpa` fits the Real-Time Prototype Analyzer (8301 ports C0–C7): events, break conditions (including pass, delay, ARM, LIM and FRZ), counters and the 128-entry real-time trace. DOS/50 `EVT`, `BIF`, `CNT`, `RTT` and `DRT` work, and RTPA.TST options 0–6 pass. `-rtpa probe` loops the data probe back to A0–A7(L) as option 7 asks, and option 7 then passes. `TEK_RTPATRACE=1` traces it. See `notes/rtpa.md`.
- `-tta` fits the Trigger Trace Analyzer (8301 ports 70–77, RAM at system-bus E800–F9FF, AMD Am9513 counters). **Partial:** TTA.TST modules 00–23 and 40–43 pass, while events, triggers, counting, interrupt and acquisition are not modelled yet. `TEK_TTATRACE=1` traces it. See `notes/tta.md`.
