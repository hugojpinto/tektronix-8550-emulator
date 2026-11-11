# TTA (Trigger Trace Analyzer): reverse-engineering notes

**Status:** partly emulated (`-tta`, `emu/src/machine/tta.{h,cpp}`, `am9513.{h,cpp}`). TTA.TST V1.3A (on `8550_DIAG.IMD`, Select mode 6) gives:

| Result | Modules |
|---|---|
| Pass | 00 detection, 10 I/O ports, 20–23 RAM (inhibit, select, data, address lines), 40–43 counter chip, register select, register bits, step counter |
| Fail | 30/31 event & trigger, consecutive events; 44–46 dynamic counter, count source selection, counter gating |
| Not yet reached in detail | 50 interrupt, 60 acquisition, probe interface |

**No software in our collection uses the TTA on the 8550.** DOS/50 V1.0 (1980) predates it, and its EVT/BIF/CNT/RTT/DRT drive the RTPA. The TTA commands (AD, DATA, BUS, PRO, QUA, CTR, EVE, COU, BRE, TS, TCLR, ACQ, DISP…) exist in the 8540's OS/40 and in later DOS/50 versions, which we don't have. Until one turns up, TTA.TST is the only consumer.

## Sources
- `sources/bitsavers/manuals/tta/`, downloaded 2026-11-11 from bitsavers (mirrorservice), with `.txt` from pdftotext:
  - **070-3761-00 "8500 MDL Series Trigger Trace Analyzer Service" (Jan 1983):** installation, jumpers and verification for both the 8540 and the 8550 (option 8550F03). No register-level description. SW1051 is a momentary switch that supports the TTA diagnostics.
  - **070-3760-01 "Trigger Trace Users" (Jan 1984):** the functional model, quoted below.
- **TTA.TST:** dump it from memory after Select mode 6 starts (`-type 31 '2\r' -type 41 '6\r' -dump F`), then `build/dasm s2650 F.8301 0 2000 6000`. Module table: strings at 3235–33BF, error codes 11/MMxx.

## Functional model (users manual, 1-2 to 1-3)
- **Four trigger channels.** Each is an event comparator plus a programmable counter. Trigger = event AND counter output. A trigger can break the program, clock or gate a counter, and drive a BNC output.
- **Comparator inputs, 64 bits:** 24 address, 16 data, 8 probe, 4 counter outputs, 1 event qualifier (BNC), and 11 emulator-dependent bus control signals. An event is the AND of the definitions over these groups.
- **Acquisition memory:** the last 255 transactions (address, data, probe, I/O type), or selected ones.
- **"Trigger N arms trigger N+1"** and consecutive-event modes (users manual section 3).

## Hardware established from TTA.TST
- **Port 70:** control. Read: the written bits plus **C0** (bits 7–6 high = board present; module 00 fails if both read 0). Known bits: bit 1 = RAM select (RAM window plus system-RAM inhibit); bits 0 and 2 are set to 1 (07) for the event tests, roles unknown.
- **Ports 71, 72, 74:** 8-bit read/write. **Port 73:** 4-bit read/write (low nibble). Module 10 checks all of these with a walking bit. In the event tests, **port 73's low nibble reads the four event comparator outputs** (module 30 expects 1, 2, 4, 8). Module 31 writes E0/D0/B0/70 to port 71.
- **Port 75:** read in modules 50/60 (3024, 304C, 3061). Unknown.
- **Ports 76/77:** **AMD Am9513** System Timing Controller, data/command. The diagnostic programs counters 5…1 with mode `0B0C` (data pointer 05…01, then `0C`, `0B`), uses `FF` master reset, `5F` load all and `17` data pointer to the master mode register, and reads the status at 77. Module 30 requires status = FF before testing ("COUNTER OUTPUTS ARE STUCK" otherwise). The port is MAME's device, verbatim (`tools/port_am9513.py`).
- **RAM window:** with port 70 bit 1 and EE = 07 (2650 4000–7FFF window to system-bus C000–FFFF), the TTA RAM answers at system-bus **E800–F9FF**, i.e. CPU 6800–79FF, in 256-byte pages 68–79. **Page 75 is absent**, and module 20 checks that system RAM is inhibited there too.
  - **Pages 68–6F (2 KB):** presumably acquisition memory, 255 × 8 bytes.
  - **Pages 70–74 and 76–79 (9 pages):** the event comparator lookup RAMs. How 64 inputs, 4 events and these 9 pages map onto each other is the open question.

### Comparator RAM initialisation (TTA.TST 31D3)
It runs with RAM selected and EE = 07:
1. Pages 70–79 (not 75) are cleared.
2. Page 70 gets FF at offsets 00, 10, 20, 30, 40.
3. Page 73 gets F0 at the same offsets.
4. Page 74 gets 00, 54, 51, 45, 15 at offsets 00…40: two bits per event, `01` = ?, with event k cleared to `00` at offset 10·k.
5. Pages 76, 77 and 78 get F5 at offsets 00…40.
6. Pages 71, 72 and 79 stay 0.

Module 30 then writes 07 to port 70, and for k = 1…4 writes the walking bit `1 << (k−1)` to page 78 at offset 10·k. Each time it expects port 73 to read that bit.

So the inputs in test mode must address every RAM at the same offset, 10·k, perhaps the 2650's last RAM address or a test register. Some pages must contribute high nibbles, or must not take part in the AND, since 71, 72 and 79 are all zero.

**Next step:** trace module 30/31's RAM writes and port reads in the emulator (`TEK_TTATRACE=1`, and a 2650 data-access trace for pages 68–79). Then test hypotheses for:
- which nibble of which page feeds which event;
- how the test-mode address is formed;
- how page 74's two-bit codes (events vs counter outputs?) combine.

Then modules 44–46 (Am9513 SOURCE/GATE inputs driven by events, fetches, triggers), 50 (interrupt: INT 29 "TTA/RTPA"; strings "TRIGGER TRAP BITS", "INTERRUPT ENABLE BITS"), 60 (acquisition) and the probe interface.
