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

## 8. How the emulator models all of this

| Real hardware | Emulator | Fidelity |
|---|---|---|
| 2650 | `emu/src/cpu/s2650` (MAME core) | instruction level, with cycle counts (one RETC timing fix) |
| LSI-11/2 | `emu/src/cpu/t11` (MAME T-11 core) | instruction level; HALT enters ODT as on a KD11 |
| Z80A (LP and emulator board) | `emu/src/cpu/z80/superzazu` | passes zexall exactly; full 16-bit I/O address and M1 marker |
| 8301 boards | `emu/src/machine/tek8301.*` | register level, checked by passing the full diagnostic suites |
| 8501 boards | `emu/src/machine/tek8501.*` | register level, except the floppy controller, which is **high-level**: command blocks are executed directly against the disk image |
| Z80 emulator board firmware | `tek8301::ep_firmware` | **high-level**: no dump survives. Its behaviour was reverse-engineered from DOS/50 (`notes/8301.md`) |
| Probe and prototype | `emu/src/host/ipc_target.*` | an external process over a socket (`notes/prototype-link.md`) |
| CT8500 | `emu/src/host/ct8500.*` | the command set from the operator manual, rendered in your terminal with ANSI codes |
| HSI cable | `main8550.cpp` | both units run in 100 µs lockstep; bytes are passed at the link's timing |

**Clocks:**
- the 2650 at 2 MHz;
- the LSI-11 at a calibrated 3.1 M T-11 units per second (set with the disk-based LTC test);
- the Z80s at 4 MHz.

Interactive runs are paced to real time; batch runs go as fast as possible.

## 9. Where to go next
- `docs/OPERATION.md`: using the system, from boot to debugging a program.
- `notes/8301.md`, `notes/8501.md`: ports, interrupts, the boot flow, and every fact found by reverse engineering.
- `notes/prototype-link.md`: connecting a target system.
- `sources/bitsavers/manuals/*.txt`: OCR of the service manuals. These are the ground truth when the notes are silent.
