# RTPA (Real-Time Prototype Analyzer) and TTA: reverse-engineering notes

Status: **investigation started, nothing emulated yet.** The diagnostic reports "NO RTPA BOARD DETECTED".

## Sources
- **8301 service manual** (070-2976-01):
  - the RTPA is an option, tested in Automatic System Verification ("RTPA TEST", error code 04/yyyy);
  - interrupts: **INT 29** "TTA/RTPA" (vector 003A), **INT 30** (003C), **INT 31** (003E). Signal list: DBG INT 29(L) and DBG INT 30(L) are debug interrupts provided by the RTPA; SLV INOP(L) is issued by the RTPA to reset active lines.
- **8540 service manual** and **OS/40 manual** (`sources/bitsavers/manuals/8540/`): the 8540's **Trigger Trace Analyzer** (TTA), a later, larger relative:
  - a Data Acquisition Probe (8 test clips), an interface panel (event qualifier BNC in, trigger BNC out) and two boards;
  - input data: address, data, 8 probe bits, up to 11 emulator bus-status bits, the qualifier and 4 counter outputs (64 bits);
  - four events, four triggers (event AND counter: break, count, or signal), four counters, and an acquisition memory of 255 bus transactions;
  - OS/40 commands AD, DATA, BUS, PRO, QUA, CTR, EVE, COU, BRE, TS, TCLR, TRA.
- **DOS/50 V1.0 commands for the RTPA** (HELP text): `BIF` (RTPA breakpoint condition), `EVT` (events 1 and 2: `EVT {1|2} {option operator value}…`), `CNT` (units of the general-purpose and delay counters), `RTT` (which transactions are stored), `DRT` (display the real-time trace buffer). Their overlays are in `8301/COMMANDS`.
- **Diagnostics 1984** (`8550_DIAG.IMD`): Select mode **4 = RTPA** (`RTPA.TST`, "8301 RTPA DIAGNOSTIC VERSION 1.2") and **6 = TRIGGER TRACE ANALYZER** (`TTA.TST`), so the TTA was also an 8550 option. RTPA.TST's options:
  1. events
  2. event 1
  3. event 2
  4. counter 1
  5. counter 2
  6. trace buffer
  7. probe

## RTPA.TST
RTPA.TST loads at 2000. To get a listing, dump it from memory while it waits at the display menu: `-type 31 '2\r' -type 41 '4\r' -type 51 '\r' -type 56 '3\r' -type 61 '\r' -dump F`, then `build/dasm s2650 F.8301 0 2000 6000`.

Test descriptions it prints, which give the event model:
```
EVT1 8-BIT ADDRESS EQUAL TEST        EVENT ADDR.=001A DATA=00 T=00 TM=00 BUS=MNIO+F    COUNT SELECTED = FETCH
EVENT1 16-BIT ADDRESS AND 8-BIT DATA TEST   ADDR.=0009 DATA=00 T=00 TM=77 BUS=MNIO+F
EVT2 16-BIT ADDRESS AND 8-BIT DATA   ADDR.=5555 DATA=CC T=00 TM=00 BUS=MNIO+WNR
EVENT1/2 16-BIT ADDRESS LESS THAN / GREATER THAN TESTS
EVENT1 16-BIT ADDRESS CONTROL LINES TEST    BUS=MNIO+F+F0(H)
EVENT1 PASS1 TEST …   COUNTER 0 WAS … SHOULD BE 7FFF   COUNTER 2 WAS … SHOULD BE AAA9
```

## Ports C0–C7: what is established so far
| Port | Use seen in RTPA.TST |
|---|---|
| C2 | **Presence:** write 00, read: **bit 4 = 1 means the board is present** (21BE–21C6, retried up to a count, then "NO RTPA BOARD DETECTED"). Also read as a status with `& 1F` (287A). Written 10 / 00 / control values |
| C6 | **index** of an internal register. Read back too |
| C5 | **data**, auto-incrementing after C6. The event-setup routine (2759–27A8) writes C2 = C3 = 10, then streams a 16-byte table as C6 index, C5 data, C5 data… |
| C4, C7 | data ports addressed through C6 (27B1–27D0: 8 bytes to C4 XORed with a mask table, then 2 bytes to C7) |
| C3 | control register (written 10 / 00 / pulsed). Read after C6 = 01/02 for counter values (expected 7F/FF, A9/AA) |
| C0 | written 01 (twice in a row in the counter test, so probably a strobe or arm) |
| C1 | read in the probe test and XORed with expected patterns: the probe inputs |

Read-backs of C4–C7 are compared after `EORI FF`: those registers read inverted (289B–28D3).

## Next steps
1. Annotate RTPA.TST fully: the event table layout (index map for ADDR, DATA, T, TM, BUS and the compare mode), counter control, trace-buffer reads, how INT 29/30 are raised, and what C0 does.
2. Disassemble the DOS/50 overlays `EVT`, `BIF`, `CNT`, `RTT` and `DRT` for the user-level semantics and register use.
3. Implement `rtpa` in `tek8301`: ports C0–C7, comparators on every emulator-processor bus cycle (the EP bus hooks already used for breakpoints), counters, the trace buffer, and INT 29/30 to the Emulator Controller.
4. Pass RTPA.TST (all 7 options), then run a DOS/50 EVT/BIF/DRT session.
5. TTA: `TTA.TST` (Select 6) and the OS/40 TTA model, later.
