# Integration guide

How other programs and new hardware join the emulated 8550. Written for people adding:
- **a target system**: another emulator, or a model of your board, acting as the prototype the 8550 debugs;
- **a host computer**: a program that exchanges files with DOS/50 over the REMOTE port;
- **a new emulator processor board** inside the 8301, such as the 6800, 8080 or 8086;
- **front-panel lamps and buttons** for any of the above.

| Surface | Where it attaches | Transport | Reference implementation | Detailed notes |
|---|---|---|---|---|
| [Target systems](#1-target-systems-the-prototype-link) | the emulator processor's probe (emulation modes 1 and 2) | 4-byte requests, 2-byte replies over TCP or a Unix socket | `tools/mini_target.py` (minimal), `tools/proto_target.py` (full), FUSE | `notes/prototype-link.md` |
| [Host computers](#2-host-computers-the-remote-port) | the 8301 REMOTE port (DOS/50 `COMM`, `HOST`) | a plain byte stream over a socket, paced at the REMOTE baud rate | `tools/host_link.py` | `notes/host-link.md` |
| [Emulator processor boards](#3-new-emulator-processor-boards) | the 8301 system bus and the Emulator Controller | C++, inside `tek8301` | the Z80 board (`ep_*`) and the 8085 board (`ep85_*`) | `notes/8301.md` |
| [Lamps, buttons, panels](#4-lamps-buttons-and-front-panels) | `tek8550_system` outputs and inputs | named values | the 8301/8501 panels | `emu/src/system/outputs.h`, `emu/artwork/` |

**The one rule that runs through all of them: be faithful to 1980.**
- A link carries only what the real hardware carried: probe pins, RS-232 bytes, board registers.
- Anything that exists only for emulation (such as the `T` time messages below) is kept minimal and marked as such.
- Additions are versioned so that existing peers keep working.

---

## 1. Target systems: the prototype link

On a real 8550, emulation modes 1 and 2 move the emulated CPU onto a **probe** that plugs into the CPU socket of the engineer's own board, the "prototype". In the emulator the prototype is a separate process. The emulator connects to it and, for every bus cycle that belongs to the prototype, sends a request and waits for the reply, exactly as the probe drove the board's bus.

```
 ┌──────────── tek8550 ─────────────┐                      ┌──── your target ───────┐
 │ DOS/50 ── Emulator Controller     │   TCP or Unix socket │ memory, I/O devices,   │
 │               │                   │  4-byte request ───▶ │ interrupt sources,     │
 │  emulator processor (Z80) ────────┼── 2-byte reply ◀──── │ its own clock          │
 └───────────────────────────────────┘                      └────────────────────────┘
       client: -target tcp:HOST:PORT                          server: listens, accepts one
```

### Which cycles reach the target

| | Mode 0 | Mode 1 | Mode 2 |
|---|---|---|---|
| Memory in blocks mapped **P** (program) | 8550 memory | 8550 memory; writes are also mirrored to the target | target |
| Memory in blocks mapped **U** (user) | 8550 memory | target | target |
| I/O (`IN`/`OUT`) | none | target (except the SVC ports while SVCs are enabled) | target |
| INT, NMI, RESET, WAIT | none | from the target | from the target |

The `MAP` command sets P and U per 128-byte block; `EM 1`/`EM 2` select the mode. Section 6 of [OPERATION.md](OPERATION.md) shows a session.

### Messages

The emulator is the client. It connects once at start-up (`-target tcp:HOST:PORT` or `-target unix:PATH`), then every exchange is synchronous: one 4-byte request, one 2-byte reply, in order.

```
request  (emulator → target)   op  addr_lo  addr_hi  data          op is an ASCII letter
reply    (target → emulator)   data  lines
```

| op | Meaning | addr | data (request) | data (reply) |
|---|---|---|---|---|
| `H` | hello, once at connect | the emulator's highest protocol version (3) | — | the version the target will speak, ≤ the emulator's |
| `R` | memory read | address | — | the byte (FF if nothing is there) |
| `F` | opcode fetch, the Z80's M1 cycle (v2+; v1 targets get `R`) | address | — | the byte |
| `W` | memory write | address | the byte | ignored |
| `I` | I/O read | the full 16-bit I/O address (A8–A15 carry A or B, as on a real Z80) | — | the byte |
| `O` | I/O write | the full 16-bit I/O address | the byte | ignored |
| `V` | interrupt acknowledge (INTA) | — | — | the byte on the data bus (IM 0 opcode, IM 2 vector) |
| `T` | time advance: microseconds of emulated Z80 time since the last `T`, sent about every 256 instructions while the program runs | microseconds | — | ignored |
| `Z` | reset | — | — | ignored |

Every reply carries the target's control lines:

| `lines` bit | Since | Meaning |
|---|---|---|
| 0 | v1 | INT asserted. It's a level: keep it set until your device has been serviced |
| 1 | v1 | NMI asserted. The emulator acts on the rising edge |
| 2 | v3 | RESET asserted by the target (its reset button or power-on). Send it as a pulse, held for at least one `T`. Never raise it in answer to `Z` |
| 3 | v3 | in the `H` reply only: target power present (the probe senses the target's Vcc) |
| 7–4 | v3 | WAIT: emulator-processor T-states (4 MHz) the target held /WAIT during this cycle, 0–15. A target with another clock converts; a 3.5 MHz Spectrum multiplies by 8/7 |

**Versions:** v1 has INT/NMI and no `F`; v2 adds `F`; v3 adds RESET, WAIT and power. A target answers `H` with any version up to the one offered, and the emulator then speaks exactly that version. Older targets keep working unchanged, and a new feature means a new version.

### What it looks like on the wire

Captured from a real session (the program from `tools/make_screenshots.sh` printing a greeting in mode 1, against `tools/mini_target.py`):

```
H 03 00 00  →  03 08     hello: the emulator offers v3; the target answers v3, power present
O 10 48 48  →  ff 00     OUT (10H),A with A = 48 'H': port 10h, and A also on A8-A15
O 10 45 45  →  ff 00     'E' ...
T 79 02 00  →  ff 00     633 µs of Z80 time have passed
T 00 03 00  →  ff 00     768 µs more
```

In this run the program itself sits in 8550 memory (mode 1, P blocks), so only its `OUT`s and the time messages reach the target. In mode 2, or with U-mapped blocks, the target also sees every `F`, `R` and `W` of the program.

### A complete minimal target

`tools/mini_target.py` is the whole protocol in 23 lines of Python: 64 KB of RAM and a console port. It has been verified with a full mode-1 session (edit, assemble, link and run a Z80 program that prints through port 10h):

```python
ram = bytearray(64 * 1024)
# ... accept one connection, then for each 4-byte request:
if op == ord('H'):   res = ver = max(1, min(a, 3))   # agree on a version <= the emulator's
elif op in (ord('R'), ord('F')): res = ram[a]       # memory read / opcode fetch (M1)
elif op == ord('W'): ram[a] = d                     # memory write
elif op == ord('I'): res = 0x02 if lo == 0x11 else 0  # port 11h: "transmitter ready"
elif op == ord('O') and lo == 0x10: print(chr(d & 0x7f), end='', flush=True)
if op == ord('H') and ver >= 3: lines |= 0x08         # v3: target power is present
out += bytes([res, lines])                          # every request gets exactly one reply
```
To try it:
```
python3 tools/mini_target.py 18550                          # terminal 1
emu/build/tek8550 -target tcp:127.0.0.1:18550 -0 disks/work.img   # terminal 2: SEL Z80, EM 1, LOAD, GO
```
`tools/proto_target.py` goes further: a console UART with input, a periodic interrupt (`--tick`), WAIT and RESET test aids (`--io-wait`, `--reset-at`), ROM images (`--rom`) and an access log (`--log`).

### Checklist for a new target
- **Answer every request with exactly two bytes, in order.** Requests may arrive in batches: read whatever arrives, split it into 4-byte requests, and reply to each.
- **Answer `H` with a version no higher than offered**, and use only that version's reply bits.
- **Keep INT asserted** until your device is serviced. The emulator takes it when the program has interrupts enabled, then sends `V`.
- **Drive your timers from `T`**, not from your wall clock. The emulator may run faster or slower than real time (turbo, batch runs), and `T` is the target's only consistent clock. If you never answer, the emulated CPU waits: that is a stopped target clock, as on a real probe.
- **RESET is a pulse you originate.** Don't echo it back on `Z`.
- **WAIT is optional.** Report it only if your hardware model has wait states, converted to 4 MHz T-states.

**Limits:**
- **Speed:** each access is one socket round trip, about 10–50 µs locally. I/O-heavy programs are fine, but mode 2 (every fetch remote) runs well below real speed.
- **Not part of the protocol:** DMA from the target (BUSRQ), memory-space qualifiers, and bus timing beyond WAIT. A target with any of these would need a new version.

---

## 2. Host computers: the REMOTE port

The 8301's REMOTE port is an RS-232 line to a host computer. In the emulator it's a raw, 8-bit-clean byte stream:
- `-remote tcp:HOST:PORT` connects to your host program;
- `-remote listen:PORT` waits for one on 127.0.0.1;
- `-remote unix:PATH` uses a Unix socket.

Bytes flow at the REMOTE baud rate (`-remote-baud`, default 2400), as through the real ACIA. There is no framing or extra protocol: what DOS/50 writes to the port is what your program reads.

Two DOS/50 commands use the port:
- **`COMM`** makes the 8550 a terminal on your host. Transfers use **Standard Tekhex**:
  - data blocks `/AAAACCSS…SS`, at most 30 bytes each;
  - termination `/TTTT00SS`;
  - abort `//message`.
- **`HOST`** makes the 8550 the host for another lab running COM/COMM.

The details a host implementation must respect were found by tracing DOS/50's own code (`notes/host-link.md`):
- **COMM discards the echo of every line it sends,** including its ACK/NAK lines. A host without echo loses data unless it is configured for that (`E=L`).
- **Send a few fill characters (spaces) before each block,** so COMM has time to finish the previous one. The manual suggests 4–6 at 2400 baud; `tools/host_link.py` sends 8.
- **Handshake:** you send a block, CR, then your prompt. COMM answers `0` (ACK) or `7` (NAK), then CR.
- **HOST expects human typing:** at full line speed it drops characters. Pace a request line at about 15 ms per character or slower.

`tools/host_link.py` implements a complete host (formatted and ASCII transfers in both directions). `tools/test_host_link.sh` checks it against DOS/50 (7 checks). Two emulated labs can also talk to each other: run one with `-remote listen:P` and `HOST`, and the other with `-remote tcp:127.0.0.1:P` and `COMM`.

---

## 3. New emulator processor boards

The 8550 debugged other processors through plug-in **emulator processor boards**. The Z80 board (8300E04) and the 8085 board (8300E06) are emulated. The 6800, 8080, 8086 and others are not yet; see the [roadmap](../ROADMAP.md). A new board has two partners: **DOS/50**, which drives it through its control port and its firmware, and the **Emulator Controller**, which supplies breakpoints, single-cycling and the SVC mechanism to whichever board is fitted.

### What DOS/50 expects from a board

The life cycle, as DOS/50 V1.0 drives the Z80 board (the 8085 board is the same with its own port):

```
SEL <chip> / EM n    DOS/50 selects the board: control port <- 80 (firmware call, mode 0) or C0 (modes 1/2)
LOAD                 the program goes into program memory (mode 2: into the target, via firmware PUT)
GO                   firmware RESTORE (registers from the register block) --> board answers with an SVC
                     DOS/50 swaps memory back, control <- 92 (run), forced jump to the user PC (FB:FA)
... program runs ...  the 2650 is halted; the board's CPU owns the bus (master/slave)
break                BP1/BP2, single cycle, Esc Esc, or an SVC from the program
                     control <- 80, forced jump to the firmware DUMP entry --> board answers with an SVC
DS                   DOS/50 reads the register block and shows it
```

**Emulator Controller registers** (common to all boards; `notes/8301.md`). Note how many are **active low**:

| Port | Function |
|---|---|
| F9 (write) | D0 = 0 forced reset; D1 = 0 forced interrupt (Z80: NMI, 8085: TRAP); D2 = 0 forced jump to FB:FA; D3 = 1 debug interrupts enabled; D4 = 1 SVCs enabled |
| FA/FB, F5 | jump address, **active low** (FB:FA = FEFF starts at 0100) |
| FC–FF, F6/F7 | breakpoint registers, **active low** |
| F8 | debug enables, **active low** |
| F2 | pending events (read active low); F4 = extended bank, **active low** |
| program `OUT (F8h−n)` | SVC n (n = 1…8), raises 2650 interrupt 15+n. An SVC stops an emulator board until the next F9 write (the Language Processor resumes at once instead) |
| CPU HALT | "slave halted": F2<4>, interrupt 27 |

Events are reported in a fixed order, after the instruction completes: SVC immediately, then BP1 > BP2 > single cycle > halt.

**Board-specific parts.** Each of these must be found for the new board:
- **Control port and "present" bit:** the diagnostics detect each board with one port test. The 3.0 diagnostic's configuration probe lists all of them, which tells you the new board's control port before you write any code:

| Board | Detected when |
|---|---|
| Z80 (emulated) | `E3 & 20 ≠ 0` |
| 8085 (emulated) | E5 ← 86, forced jump, HALT: the board's CPU must interrupt the 2650 |
| 8080 | E1 ← E0, `E1 & 88 ≠ 0` |
| 6800 | E2 ← 80, `E2 & 80 ≠ 0` |
| 6809 / 6801 | `A5 & 05 = 01` / `A5 & 04 ≠ 0` |
| 8048 | A2 ← E0, `A2 & 88 ≠ 0` |
| 9900 | E4 ← FF, `E4 ≠ 0` |
| Z8000 | AF ← 80, `8B & 40 ≠ 0` |
| 68000 | `AD & 17 ≠ 0` |
| 8086 / 80186 | A7 ← 7F, `A7 ≠ 0` / 27 ← 7F, `27 ≠ 0` |
| NSC800 | A8 ← 7F, `A8 ≠ 0` |

- **Firmware PROM:** visible in program memory at F800 in firmware mode (control bit 7 = 1, bit 4 = 0). Its **entries** are reached by forced jump: restore, dump and, for the prototype, get/put. Each answers with a debug SVC (7 for dump/restore, 8 for get/put). Until the PROM is dumped, emulate the entries at high level, as `tek8301::ep_firmware` and `ep85_firmware` do. The board's diagnostic checksums the PROM, so that one test keeps failing (as 07/00D0 and 06/00D0 do today); say so plainly.
- **Register block:** the board's saved CPU state, at program memory 0000 upwards, swapped by DOS/50 around each firmware call. Its layout is CPU-specific: 27 bytes for the Z80, 14 for the 8085. Find it by filling the block with marker bytes and reading the `DS` display.
- **Forced interrupt:** what F9 D1 drives on this CPU (NMI on the Z80, TRAP on the 8085). The diagnostic tests it.

### Where it goes in the code

1. **CPU core.** Port it from MAME into `emu/src/cpu/<name>/`, changed as little as possible. Mark each change `[tek8550]`, keep the BSD-3 headers, and record the MAME commit in `reference/mame/SOURCE.txt`. Run the core's own tests (the 8085 core passes TST8080 and 8080PRE).
2. **Board state in `tek8301`.** Model it on the 8085 board:
   - `ep85_present`/`ep85_enabled()` and the control register `m_e5`;
   - the control port in `io_read`/`io_write`, returning the present and halted bits;
   - the CPU's bus routed through `slave_rd`/`slave_wr`, so the memory map, write protection and breakpoints (`bp_check`) apply;
   - `ep85_run` drives the CPU while the 2650 is halted, and raises Emulator Controller events through `ec_interrupt`.
3. **Firmware.** Write `<name>_firmware(entry)`, decoding the entries by address and answering with the debug SVC.
4. **Modes 1 and 2.** Route the board's prototype cycles to `prototype_bus` (`emu/src/machine/prototype.h`). The prototype link carries Z80 pin semantics; a board with other pins (the 8085's RST 5.5–7.5, SID/SOD) needs a new protocol version first. This is why the 8085 board's modes 1 and 2 are still open.
5. **Option.** Extend `-ep` in `emu/src/system/options.cpp` and `tek8550.cpp` (`-ep z80|8085|none` today).
6. **Validate**, the way the existing boards were:
   - the board's diagnostic from the diagnostic disc (Select mode, with the test number in the table above);
   - Automatic System Verification detecting it;
   - DOS/50 with the board's support disc installed: `ASM`, `LINK`, `LOAD`, `GO`, `BK`, `DS`, `TRACE`.

   To read a diagnostic's expectations, dump it from memory (`-dump F`, then `emu/build/dasm s2650 F.8301 …`) and follow its table-driven tests.
7. **Record** the established facts in `notes/8301.md`, each with how it was established (manual section, ROM address, diagnostic test). Add a `CHANGELOG.md` entry and update `ROADMAP.md` and the README.

---

## 4. Lamps, buttons and front panels

Front ends never look inside machines. They read **named outputs** and send **named inputs** through `tek8550_system` (`emu/src/system/tek8550.h`), the way MAME's output manager works.

- **Outputs** are integers keyed by name, with 1 meaning lit: `8301:system`, `8501:busy`, `drive0:active`, and so on. Publish a new lamp in `tek8550_system::update_outputs()`. That runs every 100 µs slice, so compare against the previous value first and call `out.set(name, value)` only on a change. Lamps that flicker (like DMA) are averaged per frame by the GUI, which gives them a natural brightness.
- **Inputs** are actions: `8301:restart`, `8301:modesw`, `driveN:eject`. Add new ones to `tek8550_system::input()`.
- **Headless tests:** `-outputs FILE` logs every lamp change with its machine time, so a board's lamps can be checked in batch runs, with no GUI.
- **Artwork:** panels are MAME `.lay` files in `emu/artwork/`, generated by `tools/gen_panels.py`. An item's `name=` binds a lamp to an output and `inputtag=` binds a button to an input. The GUI picks up any new output or input name with no code change. The format subset is described in `emu/src/frontend/sdl/layout.h`.

---

## 5. Conventions and acceptance

- **Fidelity:** model the 1980 hardware and nothing more. A link carries real signals; an emulation-only addition (like `T`) is minimal and documented as such. Where something had to be inferred, say so in the notes.
- **Compatibility:** protocol changes are additive and versioned, and older peers must keep working. The machine code stays structured as MAME devices, one class per board, so it can be upstreamed.
- **Determinism:** the system has no notion of wall-clock time. Anything that advances emulated time (a board, a link) must work identically in real-time and batch runs. Batch runs are how the regression checks, screenshots and tutorial pictures are made.
- **Regression checks:** run the list in `CLAUDE.md` before submitting. It covers both diagnostic discs, the RTPA, the host link, the screenshots, the floppy round trip and the GUI capture. Nothing that passes today may fail afterwards.
- **Licensing:** new original files carry `SPDX-License-Identifier: GPL-2.0-or-later` and their author's copyright line; ported files keep their own licence (see [NOTICE.md](../NOTICE.md)).
