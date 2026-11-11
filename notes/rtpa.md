# RTPA (Real-Time Prototype Analyzer)

**Status:** emulated (`-rtpa`, `emu/src/machine/rtpa.{h,cpp}`).
- **Diagnostic:** RTPA.TST V1.2 options 0–6 pass. That's all 16 event tests, the counter test, both counter options and the trace-buffer option, over hundreds of iterations. Option 7 (probe) passes with `-rtpa probe`. Automatic System Verification with `-rtpa` completes with no failures.
- **DOS/50:** `EVT`, `BIF`, `CNT`, `RTT` and `DRT` work.
- **Not emulated:** the TTA (Trigger Trace Analyzer, Select mode 6).

No register documentation exists. Everything below was established from:
- **the diagnostic** `RTPA.TST` on `8550_DIAG.IMD` (Select mode 4). It is table-driven: each of its 16 test records holds the register setup, the expected results and a description (record table at 2ABD);
- **DOS/50 V1.0's command overlays** in `8301/Z80/`: `RTT`'s keyword/value table, the `GO` overlay's INT 29 handler at 681E, and `DRT`'s display loop;
- **the 8301 service manual:** interrupt numbers, and the DBG INT 29/30 and SLV INOP/OPREQ signals.

How to reproduce:
- **Runs:** `TEK_RTPATRACE=1` logs every port access, comparator match and break.
- **Diagnostic listing:** dump it while it waits at the display menu (`-type 31 '2\r' -type 41 '4\r' -type 51 '\r' -type 56 '3\r' -type 61 '\r' -dump F`), then `build/dasm s2650 F.8301 0 2000 6000`.
- **Its Z80 test program:** `build/dasm z80 F.prog 0 0 40`, from a dump taken during a test.

## What the board does
The board watches the **slave bus**: the emulator processor, or the Language Processor, since RTPA.TST runs its test program on the LP with E3 = 00.
- **Events:** two event comparators.
- **Counters:** two Intel **8253** counter/timers, one per channel, providing the pass count, the general-purpose counter and the delay.
- **Trace:** a 128-transaction trace RAM. The 1982 technical catalogue (p. 30) describes each word as 48 bits: 16 address, 8 (or 16) data, 8 probe, 3 cycle type and "5 bit data used internally to identify last start/stop of the emulator processor". That is the run tag DOS/50 steps by 08 at every GO, in bits 7–3 of the entry's bus byte (below), found from DOS/50's DRT before this was known. It also says a display of the whole buffer separates the runs with a blank line, and that the trace can be kept pre-, centre- or post-trigger.
- **Results:** PC latches and a status byte.
- **Break:** on a trigger it stops the processor (SLV OPREQ) after the current instruction and raises **DBG INT 29**, which is vector 003A ("TTA/RTPA (INT 29)", diagnostic number 35).
  - **One break per trigger.** When the Emulator Controller takes over (F9), the hold ends. The status (C2: stopped, event) stays until a C2 write re-arms the board, but the same trigger does not break again.
  - **Evidence:** the system verification (ASV) runs RTPA.TST before the Z80 board test. RTPA.TST's last trigger otherwise stopped the Z80 test's first instruction (07/0000 "EMULATOR NMI CLEAR"). DOS/50's `GO` after an RTPA break also stopped again after one instruction instead of at the next occurrence of the event.

## Ports C0–C7
| Port | Write | Read |
|---|---|---|
| C0 | strobe: clocks channel 1's 8253 counters 0 and 1. The arm sequence uses it to load the pass and G.P. counts | — |
| C1 | trace qualifier (RTT): 01 F fetch, 02 I I/O, 04 R read, 08 W write, 10 M memory, 00 ALL (IR 06, IW 0A, MR 14, MW 18) | trace buffer, 6 bytes per entry (below) |
| C2 | trigger control (BIF); writing it re-arms | status: 10 board present (the diagnostic's presence test), 08 stopped, 01 / 02 event 1 / 2 fired |
| C3 | bit 3 **run enable** (DOS/50 clears it after a break, freezing the trace); bit 4 channel 2 pass enable, whose rising edge also strobes channel 2's counters 0 and 1; bit 5 channel 2 delay enable; bits 2–0 **G.P. counter source** (CNT): 0 evt1, 1 evt2, 2 trce (stored entries), 3 ftch, 4 cycl (bus cycles), 5 emul (processor clocks), 6 msec, 7 usec | 8253 counter selected by C6, low byte then high byte |
| C4 | comparator RAM byte at index C6 (bit 7 of C6 ignored) | PC-LAST low, inverted |
| C5 | 8253 register at index C6 = 2·A + channel: A = 0 pass count, 1 G.P. counter, 2 delay, 3 control word. DOS/50 and the diagnostic write `34 72 B0` / `34 72 B4`: counter 0 mode 2, counter 1 mode 1, counter 2 mode 0 / 2 | PC-LAST high, inverted |
| C6 | index | PC-NEXT low, inverted |
| C7 | compare mode at index C6, auto-incrementing: [0/1] address mode of event 1/2: 02 low byte equal (`:`), 04 equal (`=`), 08 ≤ (`<`), 01 ≥ (`>`), 00 any; [2/3] data mode: 02 equal | PC-NEXT high, inverted |

**Comparator RAM** (C4): 8 fields per event, at index 2·field + event:

| Field | Contents | Stored |
|---|---|---|
| 0 | address low | inverted |
| 1 | data | inverted |
| 2 | address high | inverted |
| 3 | unknown (always written FF) | — |
| 4 | bus value | as is |
| 5 | probe pattern T | inverted |
| 6 | bus mask | as is |
| 7 | probe mask TM | as is |

A cycle's probe inputs match when (probe XOR T) AND TM = 0. RTPA.TST option 7 established fields 5 and 7. It asks for "the Data Acquisition Probe [on] A0-A7(L)" and expects the event with T=CC, TM=FF to fire on the fetch at 0033, since CC is 33 inverted.

Bus bits: 01 memory (MNIO), 02 write (WNR), 04 fetch (F), F0 control lines (inactive high). A cycle matches when (cycle & mask) = (value & mask). The comparisons are inclusive: EVT2 "less than 5555" fires on the write to 5555.

**C2 (BIF):**

| Value | Meaning |
|---|---|
| 01 | event 1 breaks |
| 08 | event 2 breaks |
| 09 | either breaks (IND) |
| 03 (02 + 01) | LIM: both in the same cycle. The combined match is a channel-1 occurrence, so channel 1's pass count and delay apply, and then both events trigger |
| 0C (04 + 08) | ARM: event 1 arms event 2, and the G.P. counter counts once armed. Event 1's pass count or delay applies before it arms; event 2's applies once armed |
| 0C + C1 bit 20 | FRZ: as ARM, and the trace freezes at the arming event. This is inferred, see below |
| 10 | channel 1 pass count |
| 20 | channel 1 delay |

DOS/50's BIF writes C2 as follows: `1` 01, `2` 08, `IND` 09, `LIM` 03, `ARM` 0C, and `FRZ` 0C with C1 bit 20 set. `EVT n P=n` sets the pass count (C5 index 0/1 = n, plus enable 10 in C2 or C3). `EVT n C=n` sets the delay (index 4/5 = n−1, plus enable 20). `T=bbbbbbbb` is the probe pattern in binary, `A=`/`A<`/`A>`/`A:` the address, and `D=` the data.

**8253 behaviour that the tests pin down:**
- **Loading:** a count loads on the first clock after it is written; later clocks count down.
- **Pass count** (mode 2, clocked by the channel's event occurrences): the trigger is its output's rising edge at reload, i.e. on the N-th occurrence, given the load strobe at arm.
- **Delay** (clocked by fetches after the event):
  - mode 0 triggers when the count reaches zero;
  - mode 2 triggers at its reload edge;
  - DELAY1 (12) fires on the 13th fetch after the event, and DELAY2 (15) on the 16th.
- **G.P. counter:** counter 1 of channel 1. It is loaded with FFFF and counts down, so it reads back inverted as the count.
- **msec:** channel 2's counter 1 prescales a 1 µs time base. DOS/50 loads it with 1000.

**PC latches:**
- PC-LAST is the start address of the instruction during which the trigger occurred;
- PC-NEXT is the address after it;
- the processor stops after that instruction.

**Trace entries:** read through C1, oldest slot first, from a 128-slot circular RAM that starts at the write pointer, so the newest entry comes last. The six bytes per entry are:

| Byte | Contents | Read |
|---|---|---|
| 1 | address low | inverted |
| 2 | address high | inverted |
| 3 | bus byte | as is |
| 4 | data | inverted |
| 5 | extension | inverted |
| 6 | probe | inverted |

The bus byte's bits 2–0 are the cycle type. Bits 7–3 are a **run tag**: the upper bits of event 1's bus-value register, which DOS/50 steps by 08 at every `GO`. `DRT` lists only the entries whose tag matches the last run (DRT 6181, against 6D99).

## DOS/50 integration
- **F2 bit 2:** an RTPA break also sets the Emulator Controller's pending register F2 **bit 2**, which is read active low. DOS/50's `GO` handler for INT 29 (681E) tests it before reading C2. It also latches PC-next as a breakpoint does, so `DS` resumes after the instruction.
- **E3 bit 4** ("processor inactive") is set while the RTPA holds the processor.
- **Release:** the hold releases on the next F9 command, like an SVC stop.

Verified sessions (`SEL Z80`, `EM 0`, program patched with `P`):
- `EVT 1 A=0105 B=F`, `BIF 1`, `GO 100` → `0105 <BREAK TRIG1>`, with `DS` showing PC = 0106.
- `DRT` → the run's transactions with mnemonics and bus letters. After `RTT MW` only the memory write remains.
- `CNT F/C/E/U/T/1` on a 10-pass loop gives 42 fetches, 54 cycles, 269 clocks, 67 µs, 54 trace entries and 1 event, exactly as computed by hand.
- `BIF 2` with a non-matching event 2 doesn't break. `BIF ARM` breaks on event 2 only after event 1.

## Found on the way
The RTPA trace showed our Z80 core reading 16-bit operands high byte first. `rw()` used two calls in one expression, whose order C leaves unspecified. It now reads low then high, and PUSH writes high then low, as a real Z80 does (`[tek8550]` in `cpu/z80/superzazu`).

## Combinations verified in DOS/50
The program is `LD HL,000A` followed by 10 passes of `DEC HL / LD A,H / OR L / JR NZ`. All results match the hand-computed fetch numbers:

| Setup | Result |
|---|---|
| ARM, event 1 0103 with P=3, event 2 0105 | break at 0105, 2 fetches counted since arming |
| ARM, event 1 with C=3 | break at 0105, count 3 |
| ARM, event 2 with P=2 | break at 0105, count 6 |
| LIM, A≥0104 with P=3, together with A≤0105 | break at 0104, count 7 |
| FRZ, event 1 0104, event 2 0106 | break at 0106, and `DRT` shows only 0100, 0103 and 0104 |

`-rtpa probe` with `EVT 1 T=11111010` breaks at 0105, and `DRT`'s EXTERNAL column shows each address's low byte, inverted.

**FRZ is inferred.** Its only hardware difference from ARM is C1 bit 20, and no test pins down what that bit does. "The trace freezes at the arming event" is consistent with the diagnostic's ARM test, which runs with C1 = 38 and expects a trace of the writes before the arming fetch. It also gives FRZ a use of its own: keep what led up to event 1.

## INT 30 and INT 31
The 8301 manual lists them as "RTPA (INT 30)" and "RTPA (INT 31)", with signal DBG INT 30(L) "provided by the RTPA". Nothing in our sources uses them:
- DOS/50 V1.0 points vectors 003C/003E at its default handler 0040, and only 003A goes to the RTPA handler;
- RTPA.TST's interrupt handler (3A25) treats anything other than codes 11, 33 and 35 as an "unexpected interrupt" error.

The board model therefore never raises them. Their purpose (perhaps a counter output or a later DOS/50's trigger output) is unknown.

## Not modelled
- **Probe:** only two states exist. Open (`-rtpa`), where the inputs read 00, or the diagnostic's loopback (`-rtpa probe`): A0–A7(L), qualified by SLVOP. There is no way to feed it a target's signals.
- **Fields and lines:** comparator field 3, `XD`, INT 30/31 and SLV INOP.
- **The TTA** (Trigger Trace Analyzer, `TTA.TST`, Select mode 6), the later analyser, is not emulated.
