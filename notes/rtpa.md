# RTPA (Real-Time Prototype Analyzer)

**Status:** emulated (`-rtpa`, `emu/src/machine/rtpa.{h,cpp}`). The same files serve the sibling 8002A emulator (`../tektronix-8002-emulator`), whose notes (`notes/rtpa.md` there) cover the 8002 side.
- **Diagnostic:** RTPA.TST V1.2 options 0–6 pass. That's all 16 event tests, the counter test, both counter options and the trace-buffer option, over hundreds of iterations. Option 7 (probe) passes with `-rtpa probe`. Automatic System Verification with `-rtpa` completes with no failures.
- **DOS/50:** `EVT`, `BIF`, `CNT`, `RTT` and `DRT` work. Tutorial: [docs/RTPA.md](../docs/RTPA.md).
- **On the 8002:** the RTPA users guide's seven procedures (070-2785 §3, about 80 printed displays of a real 8002A) are reproduced by the 8002's `tests/rtpa_guide.sh`; its diagnostics' hardware analyzer tests (D3A21, D3A30) pass all but a few timing subtests.
- **Not emulated:** the TTA (Trigger Trace Analyzer, Select mode 6) is a separate board (`notes/tta.md`).

The 8550 has no register documentation of its own. The model was first established from the sources below, then checked against the 8001/8002 documentation of the same board (see "Documentation of the board" further down):
- **the diagnostic** `RTPA.TST` on `8550_DIAG.IMD` (Select mode 4). It is table-driven: each of its 16 test records holds the register setup, the expected results and a description (record table at 2ABD);
- **DOS/50 V1.0's command overlays** in `8301/Z80/`: `RTT`'s keyword/value table, the `GO` overlay's INT 29 handler at 681E, and `DRT`'s display loop;
- **the 8301 service manual:** interrupt numbers, and the DBG INT 29/30 and SLV INOP/OPREQ signals.

How to reproduce:
- **Runs:** `TEK_RTPATRACE=1` logs every port access, comparator match and break.
- **Diagnostic listing:** dump it while it waits at the display menu (`-type 31 '2\r' -type 41 '4\r' -type 51 '\r' -type 56 '3\r' -type 61 '\r' -dump F`), then `build/dasm s2650 F.8301 0 2000 6000`.
- **Its Z80 test program:** `build/dasm z80 F.prog 0 0 40`, from a dump taken during a test.

## An independent source: Millennium's 9508 (2026-11-15)
Millennium Systems' 9508, a 6800 system on 8001/8002 boards, drives the same board ("READ RTPA
STATUS") at 80–87, and its real-time analyzer source documents every register: S171RTA on the
8002 Millennium discs (`../tektronix-8002-emulator/sources/bitsavers/disks/Millennium_internal_floppies/`,
`99900823-01`), port equates at its start. Against the model here:
- **Agrees:** C0 strobe; C1 trace qualifier bits (01 fetch, 02 I/O, 04 read, 08 write, 10 memory)
  and the 6-byte trace entry (A7–A0, A15–A8 inverted; memory/write/fetch and a trace ID in bits
  7–3; D7–D0, D15–D8, test clips inverted); C3 bit 3 ("hardware analyzer on"), bits 4/5
  (channel 2 pass/delay counter active); the C5/C6 subaddresses; C7's compare modes (01 ≥, 02
  8-bit equal, 04 16-bit equal, 08 ≤); C4 fields 4–7.
- **Resolves:** C4 field 3, written FF, is D15–D8 of the event data (subaddresses 6/7, "D15-D8 FOR
  EVT 1/2"), for 16-bit processors.
- **Adds:** C1 bit 5 "stop on trigger 1", bit 6 "reset trace buf pointer"; C2 written as
  breakpoint modes (01 break on trigger 1, 02 break on EVT1 & EVT2 "limit", 04 EVT1 arms EVT2,
  08 break on trigger 2, 10 pass counter 1 active, 20 delay counter 1 active); C3 bits 2–0 a clock
  select (000 EVT1, 001 EVT2, 010 RTT stores, 011 fetches, 100 OPREQ, 101 slave clocks, 110 1 ms,
  111 1 µs); counter names: G.P. counter 1 "real time counter", counter 2 "divide by 1000";
  C4 subaddresses 10/11 are T8–T0 (probe pattern, 9 bits).
- **Changed (2026-11-15), for the 8002 port** (the same files serve both emulators): C4 writes
  auto-increment the subaddress over 16 ("16 SUB ADDRESSES (WRITE IN ORDER)"; the 8002
  diagnostics' hardware analyzer tests write all 16 after one C6 = 0), and status bit 3 at a
  break is the setting `stop_status_08` (on here; the 8002 turns it off). RTPA.TST options 0, 6
  and 7 and system verification pass as before.
- **Status bits:** the 9508 and the RTPA service manual both give C2's status as 01 trigger 1,
  02 trigger 2, 04 slave fault, 08 slave clock missing, 10 module active. The model here sets 08
  at a break (`stop_status_08`), as RTPA.TST and DOS/50 on the 8550 accept; the 8002 diagnostics
  expect it clear, and the 8002 turns it off.

## Documentation of the board (2026-11-15)
The 8550's RTPA is the 8001/8002 board's design on the 8301's bus, and two Tektronix manuals for the 8001/8002 version survive (in the 8002 emulator's `sources/bitsavers/manuals/800x/`):
- **070-2356, *Real-Time Prototype Analyzer Service* (1977):** theory of operation (§2: the comparison sequence R0–R3 per bus cycle, LIMit, ARM, the pass and delay counters) and the control, data and status bytes (§3), with the same register layout as here.
- **070-2785, *8002 Real-Time Prototype Analyzer Users Guide* (1979):** the TEKDOS commands (`EVT`, `BIF`, `CNT`, `RTT`, `DRT`), seven step-by-step procedures on an 8080 sample program with the displays a real 8002A printed (§3), and each emulator processor's clock and wait states (Appendix A).

With Millennium's 8002 diagnostics (DIP 4.4: the hardware analyzer tests D3A21 for the 8080 board and D3A30 for the 6800, each a table of register set-ups and expected results), they settled the rules below. Each changed the shared model; RTPA.TST options 0, 6 and 7 and system verification with `-rtpa` still pass here.

| Rule | Source |
|---|---|
| **LIMit:** "If the LIMit requirement is on, R1 (EVT 2 address and Control bits) will also be ANDed with R0 and R2 for EVT 1". The break is event 1's whole comparison within event 2's address and bus-type requirements; event 2's data, probe pattern, pass count and delay take no part, and LIM triggers as event 1 only | 070-2356 §2 step 4b; 070-2785 6.C (event 2's P and C change nothing), 6.D (event 2's D=FFFF is ignored); D3A21 "TRACE ALL TEST" |
| **Data compare modes:** the address codes (01 ≥, 02 8-bit equal, 04 16-bit equal, 08 ≤) apply to the data too (C7 index 2/3: "R2: Bus data >= EVT 1 required data ...") | 070-2356 §3, logical requirement file; D3A21 "TRACE FETCHES" (D ≥ C2, D ≤ C2) |
| **Status event bits:** the events whose whole comparison matches on the breaking cycle, whatever C2 enables; an event with no address requirement (C7 = 00) is not reported | D3A21/D3A30 subtests 1, 3–6 |
| **Delays count the CNT clock:** a delay counter is clocked by the source C3 selects (fetches, bus cycles, processor clocks, µs, ms, trace stores, the other event), not always by fetches. "When the event occurs, the counter is decremented by a software selected clock"; "The delay units are specified with the CNT command" | 070-2356 §2; 070-2785 2.B.1 (C=500 after `CNT C`: 739 = 239 + 500 bus cycles), 2.D |
| **Delays share the G.P. counter's gate:** under ARM the clock runs only while armed, so a delay on event 1 never runs out: "If a delay count (option C) is added to the EVT1 command line ... an EVT1 trigger cannot be generated" | 070-2785 3.B.4, 5.A.2 (the program runs on to its halt, count 0) |
| **A delay restarts at each occurrence** (after its pass count), unless one is already running | 070-2785 4.A.1: event 1 (P=10, C=20) breaks at passes 10, 20 and 30; event 2 (P=10, C=150 cycles, about 12 passes) at passes 22 and 42, not 32 |
| **ARM: event 2 only on a later cycle.** "EVT RST is generated on the next bus cycle, resetting both event F.F.s" | 070-2356 §2; 070-2785 3.B.6 (both events at 0207: the break comes one pass later, count 12) |
| **An event-2 trigger disarms (ARM and FRZ):** event 1 must come again, the G.P. counter stops, and under FRZ the trace runs again from the next instruction until event 1 freezes it: "the EVT2 trigger breaks the program execution and stops the general purpose counter. In addition, the EVT2 trigger arms the EVT1 comparator and enables the RTT buffer" | 070-2785 5.A.3–5.A.5 |
| **FRZ** is C1 bit 5, "Stop on EVT 1 (Freeze)" (inferred here before) | 070-2356 §3 trace control byte; 070-2785 5.A.1 (`DR 11` ends at the event-1 fetch) |
| **Counting stops at the break:** the bus-driven G.P. sources (bus cycles, fetches, trace stores, events) do not count the rest of the breaking instruction | 070-2785 2.B.1 (739, not 740); D3A21 "TRACE I/O WRITES ONLY" (8 entries traced, 7 counted) |
| **G.P. counts an event's comparisons** (`CNT 1`/`CNT 2`) whether or not C2 lets that event break | D3A21 subtests 3, 4; 070-2785 2.E.3 (`CNT 2` with BIF cleared) |
| **The PC latches follow every instruction** while the analyser runs, and hold the breaking one: TEKDOS shows a halt from them when the board is fitted | 070-2785 3.B.4 ("0301 76 HLT ... 0301 BREAK" in a run where the RTPA never triggers) |
| **Time counts at cycle resolution** when the host gives its bus-cycle lengths (`states_fetch`/`states_mem`/`states_io`, set per instruction): a count then starts after the arming cycle and ends with the breaking one. The 8550's boards leave them 0, and count whole instructions as before | D3A21 subtest 0D (694 8080 clocks, exact) |
| **A board's I/O page** (`io_page`): the 6800 has no I/O cycles, and its board marks FE00–FEFF, its SVC page, as I/O for the RTPA | 070-2349 §10 ("the 6800 microprocessor uses memory mapped I/O, the memory region FE00 through FEFF is reserved"); D3A30's trace expects FE00 as I/O cycles |

**The 8550's verified DOS/50 combinations** (below) are unchanged by these rules except one: ARM with a delay on event 1 (`EVT 1 C=3`) now runs to the program's halt with a count of 0, where it broke at 0105 before.

**Open:**
- **The µs time base's phase.** Microsecond counts agree with the guide's displays to its own tolerance ("may vary one or two microseconds"), but in one run (070-2785 4.A.1 with `CNT U`) the delays expire one instruction earlier than the real lab's, and an extra event-2 break appears.
- **The msec prescaler:** channel 2's counter 1 is a mode-1 one-shot (DOS/50, TEKDOS and the diagnostics all write 72) loaded with 1000 (990 by the diagnostics). D3A21 expects 1 ms counted over one 0.3 ms run and 0 over another programmed the same way; how the one-shot is triggered is unknown.

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
| C7 | compare mode at index C6, auto-incrementing: [0/1] address mode of event 1/2: 02 low byte equal (`:`), 04 equal (`=`), 08 ≤ (`<`), 01 ≥ (`>`), 00 any; [2/3] data mode, the same codes on the data (DOS/50 writes 02 equal) | PC-NEXT high, inverted |

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
| 03 (02 + 01) | LIM: event 1's whole comparison and event 2's address and bus-type requirements on the same cycle (070-2356 §2). The combined match is a channel-1 occurrence: channel 1's pass count and delay apply, event 2's do not, and it triggers as event 1 |
| 0C (04 + 08) | ARM: event 1 arms event 2 from the next cycle on, and the G.P. counter counts while armed. Event 1's pass count applies before it arms; a delay on event 1 never runs out (its clock is gated like the G.P. counter's); event 2's pass count and delay apply once armed. Event 2's trigger disarms |
| 0C + C1 bit 20 | FRZ: as ARM, and the trace stops at the arming event ("Stop on EVT 1 (Freeze)"); event 2's trigger lets it run again from the next instruction |
| 10 | channel 1 pass count |
| 20 | channel 1 delay |

DOS/50's BIF writes C2 as follows: `1` 01, `2` 08, `IND` 09, `LIM` 03, `ARM` 0C, and `FRZ` 0C with C1 bit 20 set. `EVT n P=n` sets the pass count (C5 index 0/1 = n, plus enable 10 in C2 or C3). `EVT n C=n` sets the delay (index 4/5 = n−1, plus enable 20). `T=bbbbbbbb` is the probe pattern in binary, `A=`/`A<`/`A>`/`A:` the address, and `D=` the data.

**8253 behaviour that the tests pin down:**
- **Loading:** a count loads on the first clock after it is written; later clocks count down.
- **Pass count** (mode 2, clocked by the channel's event occurrences): the trigger is its output's rising edge at reload, i.e. on the N-th occurrence, given the load strobe at arm.
- **Delay** (clocked by the CNT source after the event: fetches in RTPA.TST's delay tests):
  - mode 0 triggers when the count reaches zero;
  - mode 2 triggers at its reload edge;
  - DELAY1 (12) fires on the 13th fetch after the event, and DELAY2 (15) on the 16th;
  - each occurrence restarts it from its full count unless it is running (070-2785 4.A.1).
- **G.P. counter:** counter 1 of channel 1. It is loaded with FFFF and counts down, so it reads back inverted as the count.
- **msec:** channel 2's counter 1 prescales a 1 µs time base. DOS/50 loads it with 1000.

**PC latches:**
- they follow every instruction while the analyser runs (070-2785 3.B.4), and hold at a break;
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
| ARM, event 1 with C=3 | no break: runs to the HALT, count 0 (an event-1 delay never runs out under ARM, as 070-2785 3.B.4 shows on the 8002; this broke at 0105 before 2026-11-15) |
| ARM, event 2 with P=2 | break at 0105, count 6 |
| LIM, A≥0104 with P=3, together with A≤0105 | break at 0104, count 7 |
| FRZ, event 1 0104, event 2 0106 | break at 0106, and `DRT` shows only 0100, 0103 and 0104 |

`-rtpa probe` with `EVT 1 T=11111010` breaks at 0105, and `DRT`'s EXTERNAL column shows each address's low byte, inverted.

**FRZ, now documented.** Its only hardware difference from ARM is C1 bit 20, which was inferred here as "the trace freezes at the arming event" (consistent with RTPA.TST's ARM test, C1 = 38, and its trace of the writes before the arming fetch). The service manual names the bit "Stop on EVT 1 (Freeze)", and the users guide's procedure 5 shows the rest: event 2 lets the trace run again until event 1 comes back (rules table above).

## INT 30 and INT 31
The 8301 manual lists them as "RTPA (INT 30)" and "RTPA (INT 31)", with signal DBG INT 30(L) "provided by the RTPA". Nothing in our sources uses them:
- DOS/50 V1.0 points vectors 003C/003E at its default handler 0040, and only 003A goes to the RTPA handler;
- RTPA.TST's interrupt handler (3A25) treats anything other than codes 11, 33 and 35 as an "unexpected interrupt" error.

The board model therefore never raises them. Their purpose (perhaps a counter output or a later DOS/50's trigger output) is unknown.

## Not modelled
- **Probe:** only two states exist. Open (`-rtpa`), where the inputs read 00, or the diagnostic's loopback (`-rtpa probe`): A0–A7(L), qualified by SLVOP. There is no way to feed it a target's signals.
- **Lines:** `XD` and SLV INOP.
- **Fields and lines:** comparator field 3 is D15–D8 of the event data (9508), unused by 8-bit boards; data modes 04 (16-bit) compare the low byte only.
- **The RTPA's own interrupt lines** INT 30/31 (above).
