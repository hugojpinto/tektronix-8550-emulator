# How the Tektronix 8550 works — an architecture tutorial

This tutorial explains the 8550 in about twenty minutes, without the service manuals. For register-level detail, follow the pointers to `notes/`.

## 1. What the machine is for

The 8550 **Microcomputer Development Lab** (Tektronix, 1980) is a workstation for writing software for *another* microprocessor: the "target", for example a Z80 in a product you are designing.

On the 8550 you:
1. edit source files (`EDIT`, or the full-screen `ACE`);
2. assemble and link them for the target CPU (`ASM`, `LINK`);
3. run and debug the result on a real target CPU on an **emulator board** inside the 8550, with breakpoints, single-stepping, tracing and register display;
4. optionally plug that emulator CPU into your own hardware (the **prototype**) through a probe that fits the CPU socket.

The operating system is **DOS/50**. Our disk has V1.0, 18-AUG-80.

## 2. Three boxes

```
 ┌──────────┐  RS-232   ┌────────────────────────────┐   HSI 153.6 kbaud   ┌───────────────────────┐
 │  CT8500  │◀────────▶│  8301 MDU                  │◀──────────────────▶│  8501 DMU             │
 │ terminal │           │  Microprocessor Develop-   │  (packets, per-byte │  Data Management Unit │
 │ 80×25    │           │  ment Unit: runs DOS/50,   │   flow control)     │  LSI-11 file server,  │
 └──────────┘           │  the tools, the emulator   │                     │  two 8" floppy drives │
                        └─────────────┬──────────────┘                     └───────────────────────┘
                                      │ probe cable (modes 1/2)
                                      ▼
                               your prototype board
```

The work splits cleanly:
- The **8301** is the computer you talk to.
- The **8501** is the *disk*: a complete PDP-11 computer that does nothing but serve files over a fast serial link.
- The **CT8500** is a smart terminal with enough local intelligence for a full-screen editor.

## 3. The 8301 MDU: two CPUs that take turns, plus the target CPU

The 8301 is a card cage. Its most surprising feature is that the OS and the tools run on **different processors**.

| Board | CPU / chips | Role |
|---|---|---|
| **System Controller** | **Signetics 2650A** at 2 MHz; boot ROM 160-0728 and diagnostic ROM 160-0802; three MC6850 ACIAs (system terminal, remote host, auxiliary); 8257 DMA; IM6402 UART for the HSI | Runs the DOS/50 kernel (`8301/RESIDENT`): the command line, files, and the debug control of the target |
| **System RAM** | 64 KB | Memory for the 2650 and DOS/50 |
| **Program Memory** | 2 × 32 KB | A *separate* 64 KB address space used by the Z80s: tools, and your program |
| **Language Processor** (LP) | **Z80A** at 4 MHz (2.5 MHz in slow mode) | Runs the big tools: `ASM`, `LINK`, `EDIT`, `ACE`… They are Z80 programs loaded into program memory |
| **Emulator Controller** | ports F0–FF | Breakpoints, single-cycle, forced jumps, SVC decoding, "slave halted" detection, and the memory map with write protection |
| **Z80 emulator processor** (8300E04) | Z80A + on-board firmware | Runs *your* program: the target CPU, which can be moved into the prototype |
| **8085 emulator processor** (8300E06), option | 8085 + on-board firmware | The same role for 8085/8080 programs (`-ep 8085`) |
| **PROM programmer** (8550F30), option | an 8085 controller | Reads and programs 2708/2716/2732 PROMs (`-prom`) |
| **RTPA**, option | two comparators, two 8253s, trace RAM | Real-Time Prototype Analyzer: events, breaks, counters, a 128-transaction trace (`-rtpa`, ports C0–C7) |
| **TTA**, option (later) | RAM-lookup comparators, Am9513, acquisition RAM | Trigger Trace Analyzer: four trigger channels, 255-transaction acquisition (`-tta`; [TTA.md](TTA.md)) |

### Master and slave
Only one processor owns the bus at a time:
1. The 2650 (master) sets up program memory, starts a slave Z80 (the LP or the emulator board) and then **halts itself**.
2. The slave runs until something needs the master: an **SVC**, a breakpoint, a single-cycle step, a HALT or a write-protect violation.
3. Each of these raises a 2650 interrupt. The 2650 wakes, does the work and restarts the slave.

### SVCs: how a Z80 program asks the OS for something
A tool running on a Z80 executes `OUT (F8h−n),A` (SVC n, n = 1…8). The Emulator Controller turns this into 2650 interrupt 15+n. DOS/50 then reads a **Service Request Block** (SRB) whose address sits at program memory 40h + 2(n−1). The SRB says "open this file", "read a line from the console", and so on. DOS/50 performs the request, using DMA to move data between system and program memory, writes the status back and resumes the Z80.

This is why `ASM` running on the LP can read files that physically live on the 8501.

### Memory map and write protection
Program memory is divided into 512 blocks of 128 bytes. Each block has two bits in the map RAM:
- **U/P:** in emulation modes 1 and 2, whether the block lives in *your prototype* or in 8550 program memory;
- **read-only:** whether the block is write-protected.

A write to a protected block is suppressed, and DOS/50 reports `<BREAK MEMWR>`. The `MAP` command edits this table.

### Emulation modes
| Mode | Target CPU runs… | Memory | I/O and interrupts |
|---|---|---|---|
| **0** | inside the 8550, with no prototype needed | all 8550 program memory | none (reads FF) |
| **1** | in the prototype socket | per block: P = 8550 memory, U = prototype | prototype |
| **2** | in the prototype socket | all prototype memory | prototype |

## 4. The 8501 DMU: a PDP-11 as a disk

| Part | What |
|---|---|
| CPU | DEC **LSI-11/2**, with 32 KW of memory; addresses are octal |
| Utility board | 2 KW PROM, of which one half is power-up diagnostics and the other the bootstrap; line-time clock (LTC); front-panel LEDs; auxiliary and printer serial ports |
| Flexible disc controller | its own Z80 with discrete TTL. The LSI-11 hands it an 8-word command block (read, write, format, status…) through register 177160 |
| I/O board | the HSI link to the 8301, with DMA |
| Discs | 8" double-sided double-density: **3991 records × 256 bytes**. Track 0 side 0 is single density |

At power-up the 8501 runs its PROM diagnostics, then boots `8501.SAV` from the first drive that has a disc. `8501.SAV` is the file-server OS: it waits on the HSI for requests such as open, read, write or directory, and serves them from the discs. The 8501 also has an LSI-11 console (**ODT**) on its auxiliary port, for service work.

## 5. The HSI link
The link is a 153.6 kbaud serial connection carrying packets:
- **Request header:** starts with `81`, followed by a length and parameters, and ends with a checksum. The receiver answers `06` (ACK) or `15` (NAK).
- **Data:** travels in `02 … 03` frames, and `19` acknowledges a received frame.
- **Flow control:** works per byte. The receiver raises "not clear to send" as soon as it sees a start bit, and drops it once the byte has been read.

## 6. What happens at power-on

| Time (s) | Unit | Event |
|---|---|---|
| 0 | both | power on. The 8301 boot ROM checks the mode switches and hands over to the **diagnostic ROM** (power-up tests: RAM, parity, timer, ACIAs…) |
| ~1.8 | 8501 | PROM diagnostics pass; `8501.SAV` is booted and queues its version message |
| ~7.6 | 8301 | diagnostics pass and control returns to the boot ROM, which prints **`BOOT V2.1`** and opens the HSI |
| 7.6 | 8501 → 8301 | `8501 V1.0` arrives. The boot ROM prints it and **restarts**, which is why `BOOT V2.1` appears twice (genuine ROM behaviour) |
| 7.6–20 | both | the boot ROM asks for `/VOL/SYSTEM/8301/RESIDENT` and loads ~100 KB into system RAM over the HSI, then jumps to it |
| ~20 | 8301 | **`DOS/50 V1.0 18-AUG-80`**. DOS/50 probes the Language Processor by running a tiny Z80 program that must HALT |
| ~24 | 8301 | **`>`** prompt |

## 7. The CT8500 terminal
- **Memory:** 50 lines × 80 columns (two pages), of which 25 lines are visible. Characters are a 6×8 dot matrix in an 8×10 cell, green on black.
- **Remote control:** the computer controls the terminal with `ESC` + a code, for example `ESC 7C row col` to position the cursor, `ESC 3E` to split the screen, and attributes such as underline, blink, inverse and dim.
- **Host-control mode** (`ESC 3D`): the cursor and function keys stop acting locally and send `ESC` + a code to the computer. This is what makes `ACE`, the full-screen editor, possible.
- The boot ROM and the DOS/50 command line use only CR/LF; they scroll like a teletype.

## 8. Inside the Trigger Trace Analyzer

The TTA (option 8550F03) sits in the 8301 next to the RTPA. How to use it for analysis is in [TTA.md](TTA.md); this section is the board at register level, as the emulator models it. Everything here was established from its diagnostic, TTA.TST V1.3A (`notes/tta.md` gives the evidence for each fact). TTA.TST drives the board in its **test mode**, in which the 2650 itself is the watched bus. The run-mode details (which target signals address which comparator RAM) are not known and are modelled functionally; see the end of this section.

Addresses are as the 2650 sees them, through the diagnostic's bank window (port EE = 07: CPU 4000–7FFF = system bus C000–FFFF).


This is the board as the emulator models it, everything as TTA.TST established it. Addresses are as the 2650 sees them, through the diagnostic's bank window (port EE = 07: CPU 4000–7FFF = system bus C000–FFFF).

### Ports

| Port | R/W | Function |
|---|---|---|
| **70** | R/W | Control. **Bit 0:** comparators on (writing 0 clears the event latches). **Bit 1:** RAM select. **Bit 2:** counter clock on, and the comparator RAMs addressed by the 2650 (test mode); writing 0 clears the arming chain. Reads back with bits 7–6 high (board present). `07` = test mode, `03` = comparators on the probe, `02` = RAM access only |
| **71** | R/W | **Bits 7–4:** trigger 4–1 must be armed by the previous trigger (1 is armed by 4). **Bit 1:** acquire every access (otherwise only those on which event 4 fires) |
| **72** | R/W | Interrupt enables, one per trigger (bits 3–0). A write clears a pending TTA interrupt request |
| **73** | R/W | Trigger latches (bits 3–0), set by triggers. The 2650 writes it too (write 0 to clear) |
| **74** | R/W | 8-bit register, role unknown (the diagnostic only checks it holds data) |
| **75** | R | Acquisition pointer: where the next sample goes |
| **76 / 77** | R/W | Am9513: data / command and status |

### Memory

With port 70 bit 1 set and EE = 07, the TTA RAM answers at system bus E800–F9FF (CPU 6800–79FF) and system RAM there is switched off. It is organised as 256-byte pages:

| CPU page | Contents |
|---|---|
| 68–6F | **Acquisition memory:** 256 samples × 8 bytes, one page per byte. In test mode each byte of a sample is the 2650's A7–0; on the probe, page 6A is the probe byte |
| 70–74, 76–79 | **Comparator lookup RAMs**, one per input group, addressed by that group's value (or by the 2650's A7–0 in test mode). Each holds, per row, the events that value takes part in |
| 75 | Absent: no RAM, and system RAM is still inhibited (module 20 checks this) |

### Events and triggers

At each 2650 access to a comparator page, with port 70 bits 1–0 set, the board looks up one row in every comparator RAM and latches four events. The row is the 2650's A7–0 in test mode; page 73 uses the probe lines instead when port 70 bit 2 is clear.

- **Event k** fires when **page 73's bit 3+k is set** (high nibble, one bit per event) **and page 74's bit pair for event k is 00**. Page 74 holds two bits per event (bits 1–0 for event 1, … 7–6 for event 4). That fits the relation codes a range comparator needs, though in the tests only 00 = "match" appears.
- Event outputs are **levels**, held until the next comparator access. They hold while port 70 bit 0 stays set, even with the clock stopped.
- **Trigger k** = event k AND counter k's output. With port 71 bit 3+k set, trigger k also needs trigger k−1 to have fired on an earlier access. Triggers set the latches in port 73.

### Counters

- The Am9513 runs from **X1 = 5 MHz** while port 70 bit 2 is set. It comes from the 8301's 20 MHz oscillator, which also gives the 2650 its 2 MHz, so there are exactly 2.5 counter clocks per 2650 clock.
- **Event k drives SRC k and GATE k+1.** So a counter can count event k (count source SRC k), or count F1 only while event k−1 holds (gating "GATE N", active high or low), and so on. SRC5 and GATE1 are not driven by events.
- **Counter outputs OUT1–4 feed the triggers.** An output set to high impedance (output control 100) reads high: that is how a channel's trigger is made to follow its event alone. The status register's bits 7–6 read 1.

### Interrupt

- A **new** trigger whose port 72 bit is set raises 2650 interrupt **29** (vector 3A), the one the RTPA also uses. Triggers already latched when the enables are written do not, nor do bits the 2650 writes into port 73.
- The request shows in Emulator Controller port F2 bit 2 (read active low) until port 72 is written again.

### Acquisition

- At each comparator access, if port 71 bit 1 is set, or else if event 4 fires, a sample is stored at the pointer, and the pointer (port 75) advances by one, wrapping at 256.

### A recipe: one event, counted

These steps are put together from what TTA.TST does in modules 30 and 45. They have not been run as a separate program:
1. **Select the RAM:** EE ← 07, port 70 ← 02.
2. **Clear pages 70–79**, then set up the row you will use, say 10:
   - page 73 row 10 = F0 (all events may fire);
   - page 74 row 10 = 54 (event 1's pair 00, the others 01).
3. **Program the counters.**
   - Am9513 reset: 77 ← FF, then 77 ← 5F (load all).
   - Counter 1 mode = 0108 (count source SRC1, count up): 77 ← 01 (data pointer: counter 1's mode), 76 ← 08 (low byte first), 76 ← 01.
   - Arm counter 1: 77 ← 21.
4. **Start:** port 70 ← 07 (comparators on, test mode, clock running).
5. **Fire event 1:** each write to page 7x row 10, for example 2650 address 7810, is one event 1. Its rising edge on SRC1 is one count. Port 73 bit 0 (trigger 1) is not set, though: in this mode counter 1's output is low. For triggers that follow the event, give counter 1 a high-impedance output (mode low byte 0C, as module 30 does).
6. **Read the count:** 77 ← 9F (disarm and save all), 77 ← 19 (counter 1's hold register), then read 76 twice, low byte first.

### A walk through TTA.TST module 30

`TEK_TTATRACE=1` logs every port access, event change and acquisition, with the time in emulated seconds, exact to the 2650 clock. Here is one pass of module 30, the event & trigger test, annotated:

```
[66.0878720 TTA wr 77 01]       counter 1 mode register...
[66.0878765 TTA wr 76 0c]       ...= 0B0C: count F1 up, output high impedance
[66.0878810 TTA wr 76 0b]
[66.0879110 TTA rd 77 ff]       status FF: all counter outputs high, so triggers = events
[66.0879380 TTA wr 70 02]       RAM select: initialise the comparator pages (31D3)
[66.1202150 TTA wr 70 07]       test mode
[66.1202285 TTA wr 73 00]       clear the trigger latches
[66.1202375 TTA events 1]       the 2650 writes page 78 row 10: event 1 fires
[66.1202420 TTA rd 73 01]       trigger 1 latched: expected 0001
[66.1202705 TTA wr 73 00]
[66.1202795 TTA events 2]       row 20: event 2
[66.1202840 TTA rd 73 02]
[66.1203125 TTA wr 73 00]
[66.1203215 TTA events 4]       row 30: event 3
[66.1203260 TTA rd 73 04]
[66.1203545 TTA wr 73 00]
[66.1203635 TTA events 8]       row 40: event 4...
[66.1203635 TTA acquire 40 at 00]  ...which also takes an acquisition sample
[66.1203680 TTA rd 73 08]
[66.1204085 TTA wr 70 00]       comparators off: event latches cleared
[66.1204385 TTA wr 77 ff]       counter reset: outputs now low
[66.1204835 TTA rd 77 c1]       status C1: outputs low
[66.1204985 TTA wr 70 07]
[66.1205075 TTA events f]       row 0: all four events fire...
[66.1205150 TTA rd 73 00]       ...but no triggers: the counter outputs are low
```

### Tracing the board

| Variable | What it logs |
|---|---|
| `TEK_TTATRACE=1` | TTA ports, event changes and acquisitions (stderr) |
| `TEK_INTTRACE=1` | interrupts the 2650 takes (`INT 29` is the TTA's or the RTPA's) |

To study a module, run it alone (`-type 51 'T\r' -type 56 '46\r'`) with the trace on, and dump TTA.TST from memory to read its code:

```
build/tek8550 -tta -ep none -pm ext -term tty -0 ../sources/bitsavers/disks/8550_DIAG.IMD \
    -cycles $((45*2000000)) -type 31 '2\r' -type 41 '6\r' -dump /tmp/tt </dev/null
build/dasm s2650 /tmp/tt.8301 0 2000 2000 | less
```

The modules' entry points are listed in [notes/tta.md](../notes/tta.md).

### Run mode: watching the program under test

**On the real board.** In use, the TTA sits on the 8301's emulator bus. On every bus cycle of the emulator processor:
1. The leading edge of SLV OPREQ latches the address and control lines; its trailing edge latches the data and the probe lines (users manual §4).
2. Each latched input group (address bytes, data, probe, control) addresses its own comparator RAM. The TTA software had loaded those RAMs, for every possible value of the group, with the events that value takes part in. So a value, a range, don't-care digits or an excluded range all become a table lookup at bus speed; page 74's 2-bit codes, seen in test mode, fit relation codes for ranges spanning several bytes.
3. The RAM outputs combine into the four events about 60–80 ns after the cycle. Events feed the Am9513 (SRC k, GATE k+1, as in test mode); each is ANDed with its counter's output to make a trigger.
4. A trigger with a break raises INT 29 and holds the processor after the current instruction. Each cycle, or event 4's only, is stored in acquisition memory. The 2650 reads it all back once the emulator stops.

**In the emulator.** `machine/tta_run.*` runs the same steps per bus cycle of the emulated Z80. The difference is step 2: it evaluates the event definitions (values, ranges, masks, cycle types, counter pattern) directly instead of looking them up in RAM tables, because nothing that survives programs the board in run mode. The counters are modelled the same functional way, with the five output modes, sources, gates and restart of the users manual and the OS/40 manual's Table 5-2: the TTA software's exact Am9513 programming for each `cou` option is not known either. The timebases are the Am9513's F1–F5 at 5 MHz with decade scaling (200 ns … 2 ms), the clock found in test mode. The break holds the processor and raises INT 29, which also shows in the Emulator Controller (E3 bit 4, F2 bit 2) as the RTPA's does.

**The commands.** `system/tta_console.*` is the TTA command set of the users manual: `acq ad bre bus cons cou ctr data disp eve pro qua tclr ts`, standing in for the later DOS/50 that had it. `tek8550_system::key_in` gives it the lines typed at the DOS/50 prompt whose first word is a TTA command; DOS/50 then gets an empty line. Usage: [TTA.md](TTA.md).

**One bridge.** DOS/50 V1.0's GO handler (overlay address 681E) accepts an INT 29 break only after reading the RTPA's status register C2 (10 present, 08 stopped, 01/02 TRIG1/2). With no RTPA fitted, the emulator shows the TTA's break there, so that DOS/50 V1.0 stops cleanly and prints the processor's state. With the Z80 support it still labels the break `<BREAK   >` (it ties the TRIG names to its own `BIF` settings), so the console prints `TTA break: TRIGn` first; DOS/50's 8085 support prints `<BREAK TRIG1>` itself. The TTA watches both emulator boards (Z80 and 8085), in every emulation mode: target accesses over the prototype link are cycles of the emulator processor like any other. This is emulator glue for software we don't have, not hardware.

## 9. How the emulator models all of this

| Real hardware | Emulator | Fidelity |
|---|---|---|
| 2650 | `emu/src/cpu/s2650` (MAME core) | instruction level, with cycle counts (one RETC timing fix) |
| LSI-11/2 | `emu/src/cpu/t11` (MAME T-11 core) | instruction level; HALT enters ODT as on a KD11 |
| Z80A (LP and emulator board) | `emu/src/cpu/z80/superzazu` | passes zexall exactly; full 16-bit I/O address and M1 marker |
| 8301 boards | `emu/src/machine/tek8301.*` | register level, checked by passing the full diagnostic suites |
| 8501 boards | `emu/src/machine/tek8501.*` | register level, except the floppy controller, which is **high-level**: command blocks are executed directly against the disk image |
| Z80 emulator board firmware | `tek8301::ep_firmware` | **high-level**: no dump survives. Its behaviour was reverse-engineered from DOS/50 (`notes/8301.md`) |
| Probe and prototype | `emu/src/host/ipc_target.*` | an external process over a socket (`notes/prototype-link.md`) |
| REMOTE port / host computer | `emu/src/host/serial_link.*` | a byte stream over a socket; DOS/50 COMM and HOST (`notes/host-link.md`) |
| RTPA | `emu/src/machine/rtpa.*` | register level, reverse-engineered from RTPA.TST and DOS/50; passes its diagnostic (`notes/rtpa.md`) |
| TTA | `emu/src/machine/tta.*`, `am9513.*` (MAME) | test mode at register level (passes TTA.TST); run mode functional (§8, `notes/tta.md`) |
| CT8500 | `emu/src/host/ct8500.*` | the command set from the operator manual, rendered in your terminal with ANSI codes |
| HSI cable | `main8550.cpp` | both units run in 100 µs lockstep; bytes are passed at the link's timing |

**Clocks:**
- the 2650 at 2 MHz;
- the LSI-11 at a calibrated 3.1 M T-11 units per second (set with the disk-based LTC test);
- the Z80s at 4 MHz.

Interactive runs are paced to real time; batch runs go as fast as possible.

## 10. Where to go next
- `docs/OPERATION.md`: using the system, from boot to debugging a program.
- `notes/8301.md`, `notes/8501.md`: ports, interrupts, the boot flow, and every fact found by reverse engineering.
- `notes/prototype-link.md`: connecting a target system.
- `sources/bitsavers/manuals/*.txt`: OCR of the service manuals. These are the ground truth when the notes are silent.
