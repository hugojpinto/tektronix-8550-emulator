# Operating the emulated Tektronix 8550 — a hands-on tutorial

This takes you from a fresh checkout to editing, assembling, running and debugging a Z80 program, in about half an hour. Read `docs/ARCHITECTURE.md` first if you want to know *why* things work this way. Commands below are run from `emu/` unless noted.

## 1. Build and boot

```
cd emu
make
cp ../disks/System_Fixed.img ../disks/work.img      # your own working copy
build/tek8550 -rw -0 ../disks/work.img
```

- `-0` puts a disk image in drive 0. Use `.img` (raw) or `.imd`. Drives `-1` to `-3` work the same way.
- `-rw` saves changes back to that image, so your files survive. Without it, every write is lost at exit. Always use a copy; the originals in `sources/` are read-only by convention.
- Use a terminal of at least **80×25**. Your terminal becomes the CT8500.

After about 27 seconds (it runs at real speed, real disc seeks included) you see:
```
BOOT V2.1
8501 V1.0

BOOT V2.1

DOS/50 V1.0   18-AUG-80
>
```
The doubled `BOOT V2.1` is genuine: the boot ROM restarts after printing the 8501's version message.

**To leave:** press **Ctrl-]** to open the emulator monitor, then:

| Key | Action |
|---|---|
| `q` + Enter | quit, saving `-rw` disks |
| `c` or Enter | resume |
| `r` | registers |
| `R` | restart both units |

## 2. The DOS/50 command line

- `HELP` lists every command. The capital letters are the minimum abbreviation: `LOad` can be typed `LO`, and `Ldir` can be typed `L`.
- Commands and file names are not case-sensitive. Arguments are separated by spaces.
- Numbers are hexadecimal.

Commands for finding your way around:

| Command | Does |
|---|---|
| `LDIR` | list the current directory. `LDIR 8301` lists a subdirectory |
| `STATUS` | the selected processor and the last language loaded |
| `SEL Z80` | select the target processor. This chooses the assembler, linker and debugger behaviour; `SEL` alone shows the current one. The disk also has 8080, 8085 and 6800 support, but only the Z80 emulator board is emulated |
| `COPY FILE CONO` | print a file on the console (`CONO` = console output) |
| `COPY A B` / `DELETE F` / `RENAME A B` | file housekeeping |
| `DATE` | the clock (starts at 01-JAN-70) |

## 3. Write a program

You have two editors.

**EDIT** is a line editor and is easy to script:
```
> EDIT T.ASM
** EDIT VERSION 4.0
** NEW FILE
*INPUT
        ORG     100H
START   LD      B,10H
LOOP    INC     A
        DJNZ    LOOP
DONE    JR      DONE
        END     START
                         (an empty line ends input)
*FILE                    (save and exit: "** END OF TEXT")
```

**ACE** is the full-screen editor that uses the CT8500's host-control mode. Start it with `ACE T.ASM`:
- arrows, Home and Page Up/Down move around;
- F1–F4 scroll;
- Insert/Delete work on characters; F9/F10 insert and delete lines;
- **Esc** leaves insert mode, and `EX` + Return saves and exits.

The full key map is in `emu/README.md`; the ACE manual OCR is `sources/bitsavers/manuals/070-3573-00_ACE.txt`.

**Assembler syntax gotchas** (Tektronix ASM V4.0B):
- Strings are `ASCII "TEXT"`, not `BYTE "TEXT"`. `BYTE` takes numbers: `BYTE 0DH,0AH,0`.
- Comments start with `;`. A leading `*` is not a comment.
- An apostrophe `'` is the macro character, so don't use one in comments.
- Labels start in column 1, and opcodes are indented.

## 4. Assemble, link, load

```
> ASM T.OBJ T.LST T.ASM          object, listing, source
Tektronix        Z80 ASM V4.0B
            >>> No assembly errors detected <<<
> COPY T.LST CONO                view the listing
> LINK T.LOAD T.LNKL T.OBJ       load module, link listing, objects
8550 LINKER V4.0H
    NO ERRORS     NO UNDEFINED SYMBOLS
    TRANSFER ADDRESS IS 0100
> LOAD T.LOAD                    into program memory (mode 0)
```
ASM and LINK take tens of seconds each; these were slow machines.

## 5. Run and debug (emulation mode 0)

In mode 0 the Z80 emulator board runs your program inside the 8550, with no hardware needed.

| Command | Does |
|---|---|
| `GO` | run from the load module's start address. `GO 100` runs from 100h |
| **Esc Esc** | break a running program and show where it stopped |
| `BK 106` | set a breakpoint at 106h. `CLBP` clears breakpoints |
| `TRACE ALL` | single-step display of every instruction during `GO`. `TRACE OFF` turns it off |
| `DS` | display the Z80 registers |
| `D 100 10F` | dump memory |
| `EXAM` / `PATCH` / `FILL` / `MOVE` | inspect and change memory |

A real session with the program from section 3. `LOOP` is at 102h; a breakpoint must sit on the first byte of an instruction:
```
> LOAD T.LOAD
> BK 102
> GO
LOC  INST     MNEM OPER        SP   F  A  B  C  D  E  H  L  IX  IY
0102 3C       INC  A          0000 00 01 10 00 00 00 00 00 0000 0000
0102   <BREAK     BKPT1>
> DS
PC=0103 SP=0000       F=00  A=01  B=10  C=00  D=00  E=00  H=00  L=00
IX=0000 IY=0000      AF=00 AA=00 AB=00 AC=00 AD=00 AE=00 AH=00 AL=00
IFF1=0  IFF2=0  IM=0  I=00  R=02
> GO                              (continues: A=02, B=0F at the next break)
> CLBP
> D 100 10F
0100 06 10 3C 10 FD 18 FE ...
```
`DS` prints the alternate registers as `AF` … `AL`.

### Write protection
- `MAP` shows the memory map.
- `MAP PRO 0 0FFF` makes 0000–0FFF read-only.
- A program writing there stops with **`<BREAK MEMWR>`**.
- `MAP PRW 0 0FFF` makes the range writable again.

## 6. Talk to a target system (modes 1 and 2)

Your "prototype" is a separate process. The reference one has 64 KB of RAM and a console UART at ports 10h/11h:

```
# terminal 1 (repo root)
python3 tools/proto_target.py --tcp 18550
# terminal 2 (emu/)
build/tek8550 -rw -target tcp:127.0.0.1:18550 -0 ../disks/work.img
```

Then, on the 8550:
```
> SEL Z80
> EM 1                 select the mode BEFORE LOAD (EM resets the emulated CPU)
> LOAD HELLO.LOAD      a program that does OUT (10H),A for each character
> GO                   -> "HELLO FROM THE 8550" appears in terminal 1; Esc Esc to stop
```

- **Mode 1:** program memory stays in the 8550, unless you `MAP URW lo hi` blocks to the prototype. I/O goes to the target.
- **Mode 2:** all memory is on the target, and `LOAD` writes straight into it.

The wire protocol (so you can write your own target, or use FUSE's Spectrum as one) is in `notes/prototype-link.md`.

## 7. Talk to a host computer (COMM and HOST)
The 8301's REMOTE port connects to a host over RS-232. Here it is a socket.

```
python3 tools/host_link.py --connect 8551 --dir ~/mdl-host      # terminal 1: a small "host computer"
emu/build/tek8550 -remote listen:8551 -0 disks/<your>.img        # terminal 2
> COMM P=2420                    -> COMM V4.0, then the host's banner and "$ " prompt
$ ls                             -> host commands; type "help"
$ send prog.hex^@<CR>            formatted download: Tekhex file -> program memory (prog.bin@100 for a binary)
$ recv out.hex^@100 1FF 100<CR>  formatted upload: 0100-01FF -> Tekhex file, start address 0100
$ cat notes.txt^@>NOTES<CR>      host text file -> DOS/50 file NOTES
$ save list.txt^@<HELLO.LST<CR>  DOS/50 file -> host; when RIOT: appears, type Ctrl-D <CR>
^@<Esc>                          leave COMM
```

- **NUL:** `^@` is Ctrl-@. COMM keeps what follows it for itself.
- **Prompt:** `P=2420` tells COMM the host's prompt, "$ " in hex.
- **Two labs:** the other direction works too. One 8550 runs `HOST` and another runs `COMM P=7E7E`:
  - lab A: `tek8550 -remote listen:8552 …`;
  - lab B: `tek8550 -remote tcp:127.0.0.1:8552 …`.

  Type the request (`DHFILE.HEX`) at normal speed: HOST expects a person typing.
- **Baud rate:** `-remote-baud` sets the rear-panel baud switch (default 2400).

Details and protocol findings: `notes/host-link.md`. Regression: `tools/test_host_link.sh`.

## 8. Real-time analysis (the RTPA option)
`-rtpa` fits the Real-Time Prototype Analyzer. It watches the emulated program's bus, can break on events, counts things, and records the last 128 bus transactions.

```
> SEL Z80                         > EM 0
> EVT 1 A=0105 B=F                event 1: a fetch at 0105 (A< A> A: also work; D= data; B=MW, IR, ALL…)
> BIF 1                           break on event 1 (BIF 2, BIF ARM: event 1 arms event 2, BIF LIM, BIF CLR)
> RTT MW                          trace only memory writes (ALL, F, MR, IW…)
> CNT F                           count fetches (evt1 evt2 trce ftch cycl emul msec usec)
> GO 100                          -> 0105 <BREAK TRIG1>
> DRT                             the traced transactions, with mnemonics and bus letters
> CNT                             -> CNT=00042 ftch
```

How the board works and how it was reverse-engineered: `notes/rtpa.md`. Its diagnostic is Select mode → 4.

## 9. Program PROMs (the 8550F30 PROM programmer)

The programmer is an option, so fit it with `-prom fix`. Then put a device in its socket with `-prom-dev FILE`: a raw image, created erased if it doesn't exist and saved at exit. The emulated characteristic module takes 2708, 2716 and 2732 EPROMs.

1. **Install the PROM commands once** onto your working system disk. Put the PROM disk in drive 1 and run its install file:
   ```
   build/tek8550 -rw -0 ../disks/work.img -1 ../sources/bitsavers/disks/062-6036-01_8550F30_prom_programmer_2.1_1982.imd
   > /VOL/PP/INSTALL1
   ```
   The "Error 1D File not found" lines are expected: the script tries every processor directory.
2. **Use them:**
   ```
   build/tek8550 -prom fix -prom-dev my2716.bin -rw -0 ../disks/work.img
   > PTYPE                        device types on the module
   > RPROM 1000 2716 0 7FF        read the whole 2716 into program memory 1000-17FF
   > CPROM 1000 2716 0 7FF        compare device and memory
   > WPROM 1000 2716 0 7FF        program the device from memory
   > PSTAT                        "... NO PROM PROGRAMMER ERRORS DETECTED ..."
   ```

`-prom fix` is needed because the surviving PROM software (V2.1, 1982) was built for a later DOS/50 than our V1.0. On a real 8550 with V1.0 it would hang too; `notes/8301.md` has the details.

One known limitation: after a `WPROM`, a later `RPROM` or `CPROM` in the same session hangs. The device image is already correct, so just restart the emulator.

## 10. Run the hardware diagnostics

Both diagnostic disks run the complete self-test of every board:
```
build/tek8550 -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD
```
At the menus press **Return**, **1 Return**, **Return** (Automatic mode → System Verification → display on terminal). It takes about 16 minutes of real time. One failure is expected: **07/00D0**, the Z80 emulator board's firmware PROM checksum. That PROM was never dumped, so the emulator supplies the board's behaviour but not its bytes.

- Add `-ep none` to leave the emulator board out. Verification then ends in **`SYSTEM VERIFICATION COMPLETE`**.
- To test the Z80 emulator board on its own, choose **2** (Select mode) and then **9** (Z-80 emulator test).
- If you ever obtain a dump of the PROM (512 bytes), pass it with `-ep-prom FILE`.

`-pm ext` emulates the newer program-memory boards these diagnostics expect. DOS/50 V1.0 needs the default, older boards, so don't use `-pm ext` for normal work. Screenshots of a passing run are in `docs/screenshots/diagnostics/`.

## 11. Scripting, logging, screenshots

When stdin is not a terminal, the emulator runs flat out, at roughly 100× real time:

```
build/tek8550 -term tty -0 ../disks/work.img -cycles $((90*2000000)) \
    -type 32 'LDIR\r' -snap 40 /tmp/ldir.scr </dev/null
```

| Option | Does |
|---|---|
| `-cycles N` | stop after N 2650 cycles (2,000,000 = one emulated second) and print registers |
| `-type T TEXT` | type TEXT at emulated second T. Escapes: `\r` Return, `\e` Esc, `\xHH` |
| `-snap T FILE` | save the CT8500 screen. Render it with `python3 tools/render_screens.py [--crt] FILE out.png` |
| `-term tty` | raw text instead of the CT8500 model. `-screen FILE` dumps the screen at exit |
| `-hsilog FILE` | log the 8301↔8501 link traffic |
| `-auxlog FILE` | log the 8501 AUX port |
| `-dump FILE` | dump memory images (`.8501`, `.8301`, `.sys`, `.prog`) |
| `TEK_IOTRACE8301=1` | trace 8301 port I/O |

`tools/make_screenshots.sh` is a complete worked example: it scripts an ACE → ASM → LINK → run session.

## 12. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| My files vanished after quitting | You ran without `-rw`. Writes are kept in memory only |
| `HELP` prints "Error 06 Short or unterminated read" | You are using the original `System.imd`, whose HELP file is damaged. Use `disks/System_Fixed.img` |
| The program-memory test fails (error 02/0002) in the diagnostics | Add `-pm ext` |
| DOS/50 misbehaves with `-pm ext` | Drop `-pm ext` for DOS/50 work |
| Keys typed via `-type` are ignored | They arrived before the program was ready. Use a later timestamp (ACE needs several seconds to load) |
| ASM errors on `BYTE "..."` or `*` comments | See the syntax gotchas in section 3 |
| `LOAD` worked but the target does not see the program | `EM 1`/`EM 2` was given after `LOAD`. Select the mode first |
| The screen looks garbled | Use a terminal of at least 80×25, or `-term tty` |
| The emulator is stuck | Ctrl-] then `r` shows both CPUs' registers. `R` restarts both units |
