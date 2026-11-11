# Connecting a target system ("prototype") to the emulated 8550

On a real 8550, emulation modes 1 and 2 run your program on the Z80 emulator processor, with the CPU moved onto the **prototype control probe**. The probe plugs into the CPU socket of your own board, the "prototype". In the emulator, that board is an **external process** reached over IPC.

```
 ┌──────────── tek8550 ─────────────┐                      ┌──── target process ────┐
 │ 2650 DOS/50 ── Emulator Controller│   socket (TCP/Unix)  │ memory, I/O devices,   │
 │                    │              │  4-byte requests ──▶ │ interrupt sources,     │
 │      Z80 emulator processor ──────┼── 2-byte replies ◀── │ its own time base      │
 │      (memory map decides per      │                      │ e.g. tools/            │
 │       128-byte block P or U)      │                      │   proto_target.py      │
 └───────────────────────────────────┘                      └────────────────────────┘
```

## Which accesses go to the target

| | Mode 0 | Mode 1 | Mode 2 |
|---|---|---|---|
| Memory in blocks mapped **P** (program) | 8550 program memory | 8550 program memory; **writes are mirrored to the target** (manual: "also performed in the corresponding bytes of user prototype memory") | target |
| Memory in blocks mapped **U** (user) | 8550 program memory | **target** | target |
| Z80 I/O (`IN`/`OUT`) | none (reads FF) | **target**, except SVC ports while SVCs are enabled | target |
| INT / NMI | none | **from target** | from target |
| Write protect (`PRO`/`URO`) | all blocks | program-memory blocks only | — |
| DOS/50 access to prototype memory (`D`, `MOV PU/UP/UU`, `LOAD` in mode 2) | — | through the board firmware GET/PUT (entries F803/F800), which the emulator turns into target reads/writes | same |

Mode selection as DOS/50 V1.0 drives the board (port E3):

| E3 | Meaning |
|---|---|
| 80 | firmware call, mode 0 |
| 92 | run, mode 0 |
| C0 | firmware call, modes 1/2 |
| B2 | run, mode 1 |
| F2 | run, mode 2 |

Bit 6 or bit 5 selects the prototype; both set means all memory belongs to the prototype.
`EM n` resets the emulated CPU, so select the mode **before** `LOAD`.

## The memory map (MAP command)

- **Map RAM:** 512 entries, one per 128-byte block. D0 = U (prototype), D2 = read-only. The 2650 reaches it at system FE00–FFFF while MAPEN (EE<3>) is set, as block = (addr<7:0> << 1) | addr<8>.
- **Enables:** DOS/50 sets EA = AF while a program runs (EA<3> map/WP enable, EA<7> write-protect interrupt) and EA = 27 when stopped.
- **Write-protect violation:** the write is suppressed and the system processor gets interrupt 2. DOS/50 reports `<BREAK MEMWR>`.
- **Syntax:** `MAP` shows the map; `MAP PRW|PRO|URW|URO lo [hi] …` sets it, e.g. `MAP URW 2000 27FF PRO 0 0FFF`.

## Protocol (version 3; version 1 and 2 targets still work)

- **Version:** the emulator sends `H` with its highest version (3). The target answers the version it will speak, which must not exceed the client's. A v1 target answers 1, and the emulator then uses `R` for fetches too. A v2 target answers 2; the emulator then ignores reply bits 2–7.
- **Transport:** a stream socket. The target listens; the emulator connects (`-target tcp:HOST:PORT` or `-target unix:PATH`). It is synchronous: every request gets exactly one reply.
- **Request**, 4 bytes: `op, addr_lo, addr_hi, data`.
- **Reply**, 2 bytes: `data, lines`. Every reply refreshes the line state.

| `lines` bit | Since | Meaning |
|---|---|---|
| 0 | v1 | INT asserted (level) |
| 1 | v1 | NMI asserted (the emulator detects the rising edge) |
| 2 | v3 | RESET asserted by the target (reset button, power-on). The emulator resets the emulator processor on the rising edge. It does **not** send `Z` back, and a target must not raise RESET in answer to `Z`, so the two can't ping-pong. Send it as a pulse, held until at least one `T` has gone by |
| 3 | v3 | in the `H` reply only: target power/clock present (the probe senses target Vcc). Ignored in other replies |
| 7–4 | v3 | WAIT: T-states of the emulator processor (4 MHz) that the target held /WAIT for during this cycle, 0–15. They are added to the Z80's T-state count, so they slow the program and show in the `T` messages. A target with a different clock converts (e.g. a 3.5 MHz Spectrum's contention × 8/7, keeping the fraction) |

| op | meaning | reply data |
|---|---|---|
| `H` | hello; addr = client protocol version (1) | protocol version (1) |
| `R` | memory read at addr | byte (FF = nothing there) |
| `F` | opcode fetch (M1 read) at addr. v2 only; prefix bytes and the opcode after CB/ED/DD/FD are M1, the opcode of DD CB d op is not | byte |
| `W` | memory write data → addr | ignored |
| `I` | I/O read: addr = the full 16-bit I/O address (addr_hi = A8–A15 = A for `IN A,(n)`, B for the `(C)` forms and block I/O) | byte |
| `O` | I/O write data → I/O address (as `I`; for OUTI/OTIR B is already decremented, as on a real Z80) | ignored |
| `V` | interrupt acknowledge (Z80 INTA cycle) | byte on the data bus (IM 0 opcode / IM 2 vector low byte) |
| `T` | time advance, addr = microseconds of elapsed Z80 time, computed from the real T-state count at 4 MHz with the fraction carried over. Sent about every 256 instructions, and only while the emulator processor runs | ignored |
| `Z` | reset | ignored |

Reference target: `tools/proto_target.py`. It has 64 KB RAM, `--rom FILE@ADDR`, and a console UART at port 10h (data) / 11h (status: bit 0 input ready, bit 1 output ready). `--tick MS` adds a periodic INT that is acknowledged with RST 38h and cleared by `OUT (12h)`. `--log` logs every access. v3 test aids: `--io-wait N` (WAIT states on every I/O cycle) and `--reset-at MS` (one RESET pulse).

## Try it

```
python3 tools/proto_target.py --tcp 18550            # terminal 1: the "board"
emu/build/tek8550 -target tcp:127.0.0.1:18550 -0 disks/<your>.img   # terminal 2
> SEL Z80
> EM 1
> LOAD HELLO.LOAD          (a program doing OUT (10H),A)
> GO                       -> text appears in terminal 1; ESC ESC to stop
```
Verified: mode 1 with code in program memory and the UART on the target; mode 2 with the program loaded into target RAM (`LOAD` uses PUT) and executed from it over IPC; `MOV PU` and `D` on U-mapped memory.

## Writing your own target
Any language works. Answer every 4-byte request with 2 bytes. Keep the INT level asserted until your device is serviced (the Z80 takes it when interrupts are enabled, then sends `V`). Use `T` to drive timers.

**Performance:** one round trip per access, roughly 10–50 µs locally. That is fine for I/O-heavy code, but mode 2 (every fetch remote) runs far below real speed. A natural extension is a shared-memory RAM window (the target `mmap`s a file and declares the address ranges, and the emulator accesses them directly), keeping the socket for I/O and memory-mapped devices only. That is not implemented.

**Historical fidelity:** `F` and the 16-bit I/O address are real Z80 pins: M1 and A8–A15 during I/O cycles. A probe that replaces the CPU in its socket presents both to the target, and Z80-family peripherals rely on M1 (RETI decoding, interrupt acknowledge). `T` is not a hardware signal; it only keeps a simulated target's clock in step. None of this changes the 8550 side: DOS/50, the board firmware, mode 0 and the SVC decode (A0–A7) behave as before.

**v3 (2026-11-11), agreed with the FUSE target:** WAIT, RESET and target power are real probe pins, so they follow the "1980 probe" rule. Not done: a shared-memory RAM window (proposed for mode 2 speed and dropped: FUSE's paging, screen tracking, contention and watchpoints would all bypass it, and it isn't a probe capability). WAIT accuracy depends on how often `T` arrives (about every 256 instructions): a target whose waits depend on its own time, like the ULA's contention, gets them right on average rather than cycle by cycle. A target that stops answering (FUSE paused) just stalls the link: that is the stopped target clock.

Joint check with FUSE (EMUL 99e3d72, `fuse --machine 48 --tek-probe`): a mode-1 loop `LD A,(nnnn) / JR` reading the Spectrum's RAM. At 4000h (contended) it gets 1.08 WAIT states per read and runs 1 536 812 iterations. At 8000h it gets 0 WAIT states and 1 603 730 iterations in the same emulated time: a ratio of 0.958, against 0.959 predicted (25 vs 26.08 T per iteration). `tek8550` prints `[prototype link: N requests, W WAIT states]` at exit.

RESET was tested live with the same FUSE build, with the test loop at 0000 so that each reset restarts it. FUSE's Machine > Reset (twice) and Hard reset each reached the 8550 as exactly one `[t target RESET: …]`, and nothing was echoed back. A reset only takes effect while the emulator processor runs: the link is polled by the 8550, so a pulse sent while DOS/50 sits at its prompt is not seen. A real probe behaves the same, since RESET acts on the emulating CPU. In the first attempt the loop sat at 0100, so a reset sent the Z80 off to 0000, it hit a HALT, and DOS/50 stopped emulation. Later resets therefore had nothing to act on.

Also verified with `tools/proto_target.py --io-wait 8` (an `OUT`/`JR` loop runs at 23/31 of full speed, exactly as expected) and `--reset-at 100` (the Z80 restarts at 0000 100 ms into `GO`).

**Not modelled:** bus timing beyond WAIT, DMA from the target (BUSRQ), memory-space qualifiers, and the meaning of E3 bit 4 (DOS/50 sets it, with bit 1, while running; bit 1 unmaps the firmware PROM: `notes/8301.md`). A missing target power bit is only reported at connect time.
