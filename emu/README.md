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
### Graphical terminal (optional, SDL3)
`make gui` builds `build/tek8550-gui` when SDL3 is installed (`brew install sdl3`, `apt install libsdl3-dev`, MSYS2 `mingw-w64-x86_64-SDL3`); plain `make` never needs it. It takes every tek8550 option and shows the CT8500 in a window: the documented raster (6×8 characters in an 8×10 field, 640×250 dots, green phosphor), drawn with the screenshots' font (Tek 308 ROM upper case plus a matching lower case: the CT8500 character ROM was never dumped), as a CRT (`-style crt`, default) or sharp pixels (`-style sharp`). By default the terminal and the stacked front panels are sized together to fit the screen and centred on it (**L** re-applies that layout); `-scale F` gives fixed sizes instead. Every window can also be resized freely: the contents scale and keep their proportions.
Type straight into it: the keyboard follows the CT8500's own (map below). The window title shows the keyboard's indicators: REMOTE or LOCAL, TTY LOCK, and LEARN while a function key is being programmed.
Hotkeys (Cmd on macOS, Ctrl+Alt elsewhere): **Shift-R** REMOTE, **Shift-L** LOCAL, **Shift-T** TTY LOCK, **B** BREAK (Shift: SEND line, Alt: SEND page), **V** paste (typed at the terminal's 9600 baud, with a pause after each line), **C** copy the screen as text, **T** turbo, **F** full screen, **S** CRT/sharp, **L** fit the windows to the screen, **R** restart both units, **Q** quit (discs are written back as with the tty front end).
**Front panels** (`-panels off` to hide; Cmd/Ctrl+Alt **P** brings closed ones back): the 8301 and 8501 fronts, drawn after photos of real units, in `artwork/8301.lay` and `artwork/8501.lay` (a subset of MAME's `.lay` format: change the artwork there, no code; `-artwork DIR` picks another set). Lamps show the emulated state averaged over each frame: 8301 SYSTEM, PROGRAM, DMA, SELF TEST; 8501 ON, AC ON, PROCESSOR BUSY and each drive's activity lamp; the boards' internal diagnostic LEDs are on a strip below each unit. The switches work: the 8301 POWER and 8501 DC rockers really switch a unit off (its lamps go dark, the HSI cable goes quiet, and switching it on runs its power-up with memory cleared); the 8501 HALT/RUN switch halts the LSI-11 and RUN resumes it; RESTART holds a unit in reset for as long as it is held, at least 150 ms of machine time for a click, and starts it on release (Cmd/Ctrl+Alt **R** presses both for 200 ms). Drop a disc image on a drive to insert it, click the drive to eject it (written back first with `-rw`).
`-shot SECONDS FILE.bmp` saves the window at a machine time (and the panels as FILE_8301.bmp, FILE_8501.bmp); with `-cycles` the run is flat out, and `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software` runs it with no display.
Windows: the GUI itself is portable, but the prototype and REMOTE links (`host/ipc_target`, `host/serial_link`) still use POSIX sockets and need a Winsock shim first.

`-fdc lle|hle` picks the floppy controller: `lle` (default) runs the real board's Z80 firmware against modelled drives, `hle` the old high-level shim.
`-mount SECONDS N=FILE` inserts a disc at a machine time (the drive reports a disc change), `-eject SECONDS N` writes it back and removes it; the monitor has `m N FILE` and `e N`. Mount blank (empty) images after DOS/50 has booted: an unformatted disc at power-up delays the 8501 so much that the 8301 boot gives up, as on the real lab.
`-outputs FILE` logs the front-panel lamps (`8301:led0..4`, `8301:selftest`, `8301:ramled0..4`, `8501:led0..4`; 1 = lit) with their machine times.
The tek8550 monitor adds `o` (show the lamps and switch positions) and `p NAME [VALUE]` (switches: `8301:power`, `8501:dc`, `8501:halt`, `8301:restart`, `8501:restart`, `8301:modesw`; a RESTART with no value is a 200 ms press). Headless runs script them with `-input SECONDS NAME=VALUE` and `-press SECONDS NAME` (`-press 35 restart` presses both).
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

Key map, for both the GUI and the tty front end (CT8500 Operators Manual 070-3737 §1; ACE's configurable commands in brackets, ACE manual Fig. 4-2). Like the real terminal, the editing, paging and cursor keys act locally in power-up and local modes, and send ESC + their code in host-control mode (which ACE uses).

| Your key | CT8500 key | ACE |
|---|---|---|
| arrows / Home | cursor keys / HOME (they wrap at the screen edges as on the terminal) | (cu) (cd) (cl) (cr) (ch) |
| Ctrl-Home / Ctrl-Shift-Home | CNTRL-HOME / CNTRL-SHIFT-HOME: home of Page One / Page Two | |
| F1 F2 F3 F4 | F1–F4 (F2/F3: SCROLL) | (sl) (sd) (su) (sr) |
| F5 / PgUp, F6 / PgDn | F5, F6: PAGE (Page One / Page Two; in ACE, text towards the start / the end of the file) | (pd) (pu) |
| F7, F8 | F7 REVISE, F8 | (rm) |
| Shift-F1 … Shift-F8 | the programmable function keys (learn mode) | |
| Insert / Delete (or Shift-Insert) | INSERT CHAR / DELETE CHAR | (ic) (dc) |
| F12 / Shift-F12 | LINE INSERT / LINE DELETE | (il) (dl) |
| F9 / Shift-F9 | ERASE TO EOL / ERASE TO EOP | |
| F10 / Shift-F10 | CLEAR SCREEN / CLEAR ALL | |
| F11 / Shift-F11 | TAB SET / TAB CLEAR | |
| Tab / Shift-Tab | TAB FWD / TAB BACK | (mark) |
| Return / Shift-Return (or Ctrl-J) | RETURN / LINE FEED | (cmd-esc) |
| Backspace / Shift-Backspace | BACK SPACE (08) / RUB OUT (7F) | |
| End (or Pause) / Shift-End / Ctrl-End | BREAK / SEND line / SEND page (to the first ETX) | |
| Ctrl-Shift-F1 … F7 | learn status, enter learn, exit learn, monitor on, monitor off, host-control mode, split screen (the next character gives the line) | |
| GUI hotkeys Shift-R, Shift-L, Shift-T | REMOTE, LOCAL, TTY LOCK (TTY LOCK makes letters upper case, which DOS/50's commands need) | |
| Esc | ESC | ends insert; `EX` saves & exits ACE |

Programming a function key, as in the manual: Ctrl-Shift-F2, then the shifted key (e.g. Shift-F1), then the text (Esc + a code character runs that terminal command when played), then Ctrl-Shift-F3. A host can program them too (ESC 39 ESC 4x … US). Monitor mode is not modelled.

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
