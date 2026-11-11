# TTA run mode at register level, from the 8540's OS/40 TTA software

**Status:** the emulator's run mode follows these notes (2026-11-11): `system/tta_console.*` programs the board as OS/40 does, and `machine/tta.*` evaluates the comparator RAMs with the decision function below. They complement `notes/tta.md` (test mode, TTA.TST, and what the emulator found while implementing this).
Every fact names its evidence: a module and address in the OS/40 ROMs, a string or table, or a cross-check against a diagnostic. **Inferred** marks conclusions that the code implies but does not state.

## Sources and how to read them
- `roms/8540/160-1381-00.bin` ("TTA 1": EVE, TS, BRE; AD, DATA, BUS, PRO, QUA and CTR are aliases of EVE) and `160-1382-00.bin` ("TTA 2": COU, ACQ, TCLR, DISP, CONS, plus the resident modules TTA1 and TTA2).
- **These are 2650 programs, not PDP-11 code.** The 8540's system processor is a 2650A-1, as in the 8301 (070-3920-00 §1: "The system CPU, a 2650A-1 microprocessor"). Disassemble them with `build/dasm s2650`. The same applies to the 8088 support ROMs `roms/emulator_processor/8088/160-1461…1611`: they are OS/40 2650 modules, not PDP-11 code, which corrects `notes/8086.md`.
- **ROM file system.** Each ROM starts with a checksum (2 bytes), its part number (3 bytes) and `FF 20 00 00 00`. Then comes a directory: `DEFLT\r` followed by entries `NAME\r` + a 16-bit offset, ending in 00. The offset points at the byte before a module header.
  - **Module header:** an 8-character name field (`1R      `, `12R     `, `12ARS   `, `1RT     `, or blanks), then three big-endian words: a flag word, the length, and the load address. The data follows.
  - **Load addresses:** 8000 means the transient overlay area, whose first byte is at 2000. The overlay's first word (its abort entry, e.g. 271A for EVE) lands at 2000, and the code starts at 2002 with `zbsr *1FCC`. Resident modules carry their real address: TTA1 4100 (80 bytes), TTA2 72F0 (224 bytes). The 8088 support's EMUMON1 loads at 4008 (1F00 bytes).
  - **Disassembly:** extract the data starting 14 bytes after the header, then `dasm s2650 file <load> <load+2>`.
- **Cross-checks:**
  - the DOS/50 V2.1 8085 support disc `062-4641-03…V2.1_DOS50.imd`, file `VERSION2/EMUMON1`. Its load module maps file offset + 3FC4 to the address, so 4008 is at file 0x44. It has exactly the same 4008–40B9 layout as the 8540's 8088 EMUMON1, and the same `41 0C` TTA hook vectors at 4100. **So DOS/50 V2 used these very TTA modules and hooks** (inferred, but the layouts are identical byte for byte in structure);
  - the 8086 diagnostic's module 70 (`8086.TST` on `8550_DIAG.IMD`, file offset ~0x196F);
  - TTA.TST's RAM initialisation (file offset ~0x3209).

## 1. How OS/40 reaches the TTA
- **Ports 70–77 are read and written directly** with `REDE`/`WRTE`, as on the 8550. Every command module first checks that the board is present: `rede 70; bcfa,m error` (bit 7 set, i.e. the C0 "present" bits), or for DISP `rede 77` (Am9513 status, whose bit 7 reads 1).
- **Comparator and acquisition RAM** sit at system-bus E800–F9FF, as on the 8550, always with port 70 bit 1 set (RAM select) and interrupts off. OS/40 reaches them in two ways:
  - **The move service `zbsr *1FE0`.** EVE 28E8–294B, TCLR 2221–2284 and TTA2 7447 use it with a parameter block: +0 `3D` (function); +1…+5 zero; +6/+7 the length (big-endian; 0100 for a page, 0001 for one byte); +8…+B the source (00, a space byte, a 16-bit address); +C…+F the destination in the same form. The space byte is 31 for the transient area and 2E for the resident TTA2, so it belongs to the requester; the TTA addresses are plain 16-bit system-bus addresses (F300 = page 73, F000 = 70, F400 = 74, F6+k = 76+k, F100 = 71, F200 = 72, E8xx = acquisition page 68). The sequence is `ppsu ii`, port 70 \|= 02, call 1FE0, port 70 &= FD, `cpsu ii`.
  - **Directly through the EE window (DISP 2147–21C9).** DISP writes EE = (shadow at 3FCD & F0) \| 07 and reads the acquisition pages through pointers EF00…E800, which the 2650 sees at 6F00…6800. It then restores EE.
- **Resident hooks.** TTA1 (loaded at 4100) holds six vectors to TTA2 code, overwriting EMUMON1's default `41 0C` vectors, which point at a RETC at 410C:

  | Vector | Routine | What it does | Called from |
  |---|---|---|---|
  | 4100 | 7300 | [4044] = (port 72 & port 73 & 0F) \| 20: which enabled triggers are latched | kernel interrupt dispatch (160-1369 @0x871, `tmi r1,04; bsta,z *4100`), i.e. INT 29 |
  | 4102 | 7325 | [4044] = 0; port 73 = 00 (clears the trigger latches); port 70 \|= 01 (comparators on) | run start (inferred) |
  | 4104 | 730C | returns the latched, enabled triggers whose 4074 "continue" bit is clear, i.e. the triggers that must stop the program | break handling (160-1376) |
  | 4106 | 7480 | port 70 &= FE; Am9513 BF (save 1–5); reads hold registers 1–5 (pointers 11…15) into the channel blocks; post-processing for display | run stop (inferred) |
  | 4108 | 7333 | (re)programs the counters flagged in [410D] (§3) | GO (160-1367 GO module, 40EA) |
  | 410A | 743F | [410C] = r3 (the acquisition pointer, inferred); reads the activity byte of sample r3−1 (page 68); port 72 bit 4 = NOT its bit 0 | GO (40F8) |

## 2. Event programming (EVE and its aliases)
### Channel state in system RAM (TTA1 area, one 1A-byte block per trigger channel k = 0…3, counter 5 = k 4)
Block base 410E + 1A·k. The pointer tables are EVE 2791–27CB, COU 2633–2645, TCLR 22A7–22DD and ACQ 23A6–23B4.

| Offset | Address (k=0) | Contents |
|---|---|---|
| +00 | 410E | counter output type: 0 arm, 1 disarm, 2 timeout, 3 delay, 4 pulse (COU `o=`) |
| +01/+02 | 410F/4110 | hold register hi/lo (the count read back at stop) |
| +03/+04 | 4111/4112 | load register hi/lo (`v=`) |
| +05/+06 | 4113/4114 | Am9513 mode register hi/lo (default 0B0C) |
| +07 | 4115 | event types: low nibble address, high nibble data |
| +08…+0B | 4116–4119 | address low bound or value: [b31–24], A23–16, A15–8, A7–0 |
| +0C…+0F | 411A–411D | address high bound or mask, same order |
| +10/+11 | 411E/411F | data low bound or value: D15–8, D7–0 |
| +12/+13 | 4120/4121 | data high bound or mask: D15–8, D7–0 |
| +14/+15 | 4122/4123 | "X byte" value/mask (bus-extension bits 7–5, qualifier bit 4, counter outputs bits 3–0) |
| +16/+17 | 4124/4125 | "Y byte" (bus status) value/mask |
| +18/+19 | 4126/4127 | probe value/mask |

**Globals:**
- 410D: counters to reprogram at GO (bit k; bit 4 = counter 5).
- 410C: the acquisition pointer at the start of the current run.
- 4044: the triggers that broke.
- 4074: bits 7–4, "continue" per trigger (BRE/`-c`).
- From the emulator support (EMUMON1):
  - 40A7: feature bits. Bit 4 allows `cons fet`, bit 6 allows `cons emu`, bit 3 selects the EMUCLK code (below).
  - 40B4–40B6: the largest address (A23–16, A15–8, A7–0), also used as row masks.
  - 40B7: bit 7 means a 16-bit data bus.
  - 4020–4034: the bus-symbol tables (§4).

### Event types (EVE parser 2170–2270, 2291–23C5; keyword table 275F `AN= DN= A= D= B= P= C= Q=`)
- **Address type** ([27CD] low nibble): 1 `a=` (a value or a range), 2 `an=` (not in range), 4 a value with X digits (value plus mask), C `clr` (mask 0).
- **Data type** (high nibble): 10 `d=`, 20 `dn=`, 40 masked, C0 `clr`.
- **Rules the parser enforces:**
  - `an=`/`dn=` with X digits is rejected (error 3F);
  - a range bound may not contain X (error 2C);
  - the high bound must be ≥ the low bound (error 30);
  - `an=` covering the whole space (0 to 40B4–40B6) becomes `clr`.

### Which input addresses which RAM
Each page is two 4-bit RAM chips. **Established:** EVE computes every nibble by comparing the row number against a different input byte, and for pages 70, 73 and 76–79 the two nibbles of one row are computed from different bytes. That is only meaningful if each nibble chip has its own address input. Run-mode addressing:

| Page | Bits 7–4 addressed by | Bits 3–0 addressed by | Contents (event k = 0…3) | EVE routines |
|---|---|---|---|---|
| 76+k | **A23–16** (masked by 40B4) | **A15–8** (masked by 40B5) | high nibble: M7 M6 M5 (bit 4 unused, 0); low nibble: m3 m2 m1 m0 | A=: 2B67 / 2B8D; AN=: 2AAE / 2AE4; X: 2BCB / 2BE5; written by 2926 |
| 74 | **A7–0** | A7–0 | bits 2k+1, 2k: p1 p0 | A= 2BAD, AN= 2B26, X 2C00 |
| 72 | **D15–8** | D15–8 | 3-bit codes n2 n1 n0: event 0 in bits 2–0, event 1 in 5–3, event 2's n1 n0 in bits 7–6 | 2CCA (D=), 2C53 (DN=), 2D1D (masked), placed by 2D8D |
| 73 | **probe P7–0** | **D15–8** | bit 4+k: probe match (2E69); bit 0: event 2's n2; bits 3–1: event 3's n2 n1 n0 | 2E69, 2D8D |
| 71 | **D7–0** | D7–0 | bits 2k+1, 2k: q1 q0 | 2CFA (D=), 2C88 (DN=), 2D46 (masked); written by 2933 with F100 |
| 70 | **X byte** | **Y byte** (bus status) | bit 4+k: (X & mask) == value; bit k: (Y & mask) == value | 2E30 |

### The codes OS/40 writes
All rows are 0…FF of the indexing byte b. L and H are the bound bytes.
- **A23–16 (high nibble of page 76+k):**
  - `a=`: M7 = L≤b≤H, M6 = (b==H), M5 = (b==L);
  - `an=`: M7 = NOT(L<b<H), same M6/M5;
  - X: E0 if (b & mask) == value, else 00.
- **A15–8 (low nibble of page 76+k):**
  - `a=`: m3 = b>H, m2 = b==H, m1 = b<L, m0 = b==L;
  - `an=`, upper bytes different: m3 = b<H, m1 = b>L, m2/m0 as above;
  - `an=`, upper bytes equal: m3 = m1 = (L<b<H);
  - X: 5 (m2 m0) on a match, A (m3 m1) on a mismatch.
- **A7–0 (page 74, pair k):**
  - `a=`: p1 = b>H, p0 = b<L;
  - `an=`, upper bytes different: p1 = b≤H, p0 = b≥L;
  - `an=`, all upper bytes equal: 11 if L≤b≤H, else 00;
  - X: 00 on a match, 11 on a mismatch.
- **D15–8 (3-bit code):**
  - `d=`: n2 = outside [L,H], n1 = b==H, n0 = b==L;
  - `dn=`: n2 = L<b<H, n1/n0 as above;
  - masked: 011 on a match, 100 on a mismatch;
  - 8-bit emulators (40B7 bit 7 = 0) always use the masked form with value and mask 0, which gives 011.
- **D7–0 (page 71, pair k):** like A7–0 (`d=` like `a=`, `dn=` like `an=`, masked like X).
- **Probe** (`p=`, 24B9; value 27DE, mask FF or the X-digit mask): page 73 bit 4+k = (P & mask) == value.
- **Bus, qualifier and counters** (`b=` 23E2, `q=` 2581, `c=` 250E) go into the X and Y value/mask pairs:
  - `c=` takes up to four characters 0/1/X, the first for counter 1, into X bits 0–3;
  - `q=0`/`q=1`/`q=x` sets X bit 4;
  - bus symbols come from the emulator's tables (§4): the first table goes into Y, the second into X bits 7–5.
- **Cleared event** (TCLR 2137–214E; EVE `clr`): page 76+k = E5, pair 00 in pages 74 and 71, n = 011, page 73 bit 4+k = 1, page 70 bits 4+k and k = 1. That is "always true".

### The decision function (inferred; checked against every encoding)
For event k:

```
ADDR = M7 & !(M6 & (m3 | (m2 & p1))) & !(M5 & (m1 | (m0 & p0)))
DATA = !n2 & !(n1 & q1) & !(n0 & q0)
EVENT_k = ADDR & DATA & page73[P].bit(4+k) & page70[X].bit(4+k) & page70[Y].bit(k)
```

- This is the cascade magnitude comparator that the codes describe. M6 and M5 mean "on the high/low bound", so the next byte decides; M7 means "inside" (or "outside" for `an=`).
- **Checked by hand against every encoding:**
  - `a=` gives exactly L ≤ A ≤ H, and `an=` gives A < L or A > H, including the degenerate cases where L and H share upper bytes;
  - X masks give an equal-under-mask test, and `clr` gives true;
  - `d=`, `dn=` and masked data likewise, including the 8-bit forms.
- **Consistent with TTA.TST's test-mode rows.** Pages 76–**79** (all four, not 76–78: the init at file ~0x3243 calls the fill four times) are F5, pages 71/72 are 0, page 70 is FF, page 73 is F0, and page 74's pair 00 fires while 01/10/11 do not. In module 70, pages 76–79 = 80 make ADDR true whatever A15–0.
- **Not determined:** whether the AND with page 70 also holds in test mode. TTA.TST module 60 rewrites page 70 row 40 with pointer+80, which would block event 4 for some pointer values if page 70 were addressed by A7–0 in test mode. So in test mode X and Y probably come from elsewhere; it remains open.

### X and Y bytes (established)
- **Y = the emulator's bus-status byte**, the same byte as acquisition page 69 (both are decoded with the same tables, §4).
- **X:**
  - bits 7–5: bus-extension signals, the same bits as acquisition page 68 bits 7–5 (8088: bit 7 INT, bit 6 LCK active low);
  - bit 4: qualifier;
  - bits 3–0: counter outputs OUT4…OUT1 (bit 0 = OUT1).
- **Cross-check, 8086 diagnostic module 70:** ports 73 = 00, 74 = 75, 72 = 91, 71 = 03, 70 = 03; Am9513 FF, load 1 = 0016, 5F twice, mode 1 = 0502, E1, mode 3 = 0B04, mode 5 = 0B04, 21; pages 76 = 80, 73 = 10, 70 = 01, but 11 at rows 15, 35, 55 … F5. After master reset OUT2 and OUT4 are low, OUT3 is hi-Z (high), and OUT1 toggles high at TC. So X = xxx1 0101 exactly when counter 1 reaches TC (the 22nd cycle), with the undriven qualifier reading 1.

## 3. Counters, breaks, acquisition, consecutive events
### Count sources (COU 21B8, ACQ 2125)
The source tables at COU 2580 / ACQ 23E7 list the names in Am9513 source-code order, so source index = mode bits 11–8:

| Code | Name | Am9513 input |
|---|---|---|
| 0 | `ctr` (blank) | TCN−1 (previous counter) |
| 1–4 | `ev1`–`ev4` | SRC1–SRC4 |
| 5 | (not selectable directly) | SRC5 |
| 6 | `acq` | GATE1 = the acquisition strobe (manual: "INCR PTR") |
| 7–A | `trig1`–`trig4` | GATE2–GATE5 (GATE k+1 = trigger k, not event k) |
| B–F | `200nsec` … `2msec` | F1–F5 (5 MHz, BCD scaling) |

`cyc`, `emuclk` and `qua` all select SRC5 and set **port 70 bits 5–4: 00 = CYC (bus cycles), 01 = EMUCLK, 10 = QUA, 11 = EMUCLK as well** (11 is used unless 40A7 bit 3 is set). COU 2419 and ACQ 223C write port 70 = (70 & CF) \| sel, and warn "Counter n source redefined" when another counter's SRC5 use conflicts.

### Gating, output and restart (COU)
- **`g=`** (table 2605 `OFF CTR SELF _ TRIGH TRIGL SEQH SEQL`): the index × 20 goes into mode bits 15–13, so it is the Am9513 gating code 0–7:
  - `self` = GATE N+1 = own trigger;
  - `trigh`/`trigl` = level on GATE N = the previous trigger (GATE1 = acq for counter 1);
  - `seqh`/`seql` = edge on GATE N.
- **`o=`** (table 25E5) sets mode bits 3–0:
  - `arm` 0C: count up, output hi-Z, which reads high, so the trigger is enabled;
  - `disarm` 08: count up, output low;
  - `timeout` 02: down, TC toggled, toggle set first (E8+n);
  - `delay` 02: down, toggled, toggle cleared first (E0+n);
  - `pulse` 01: down, TC pulse.
- **`r=on`** sets mode bit 7 and requires gating.
- **`v=`** sets load and hold.
- **Default** (TCLR, `cou … clr`): type 0, mode 0B0C (F1, ungated, up, hi-Z), load and hold 0.

### Am9513 sequences
- **COU, now; TTA2 7333, at every GO** for the counters in [410D]:
  1. DF or C0\|bit (disarm), E0, n (mode register), then six bytes: mode lo, mode hi, load lo, load hi, hold lo, hold hi; then E8.
  2. 08\|n (load register). For types ≥ 2 the load becomes v−1 (v−2 with `r=on`), or 0002 if v < 3.
  3. 60\|bit, written twice (load and arm).
  4. 08\|n again: the load register becomes FFFE for arm/disarm without restart, or 0000 for the down-counting types. For v = 1 or 2 the counter is also stepped (F0+n).
  5. E8+n or E0+n sets the initial toggle for timeout/delay.
- **Reset of all counters** (COU 24A6): 4F, 01, E7 (8-bit bus), CF, E0, 01, 24 bytes, E8, 4F, 4F.
- **Stop** (7480): BF, then for n = 1…5 the pointer 10+n and two reads of 76.

### Breaks (BRE; EVE and COU `-s`/`-c`; TCLR)
- **Port 72 bits 3–0 = break enable** for triggers 1–4: `stop` and `cont` set it, `clr` clears it.
- **Stop or continue is software only:** 4074 bit 4+k set means continue.
- At INT 29 the kernel calls 7300, then 730C to decide whether to stop. This matches the INT 29 and F2 bit 2 findings in `notes/tta.md` (the kernel tests r1 bit 2 before calling *4100).

### Acquisition (ACQ, DISP, TCLR)
- **`acq all`:** port 71 \|= 02 **and** port 72 \|= 20. **`acq ev4`:** 71 &= FD, 72 &= DF. TCLR of counter 5 sets both.
- **`for N src [aftertrig4]`** programs **counter 5**: mode hi = source code (plus C0, edge gating on GATE5 = TRIG4, for `aftertrig4`), mode lo 02, set toggle (ED), load and arm (70).
  - **Inferred:** acquisition continues while OUT5 is high, so it stops N counts after the gate, which matches the users manual's semantics.
- **Port 72 bit 4 = run marker**, recorded as bit 0 of acquisition page 68. 743F flips it at GO so that a new run's samples differ from the last stored one. DISP 20F6 and 22D6 use it to find where the current run starts and to print a dotted separator between runs. The 8086 diagnostic writes 72 = 91 and expects page 68 bit 0 = 1.
- **The acquisition record**, as DISP reads it:
  - 6F/6E/6D: address (A23–16 … A7–0);
  - 6C/6B: data;
  - 6A: probe;
  - 69: bus status (decoded with tables 4024/4026);
  - 68: bits 7–2 decoded with tables 402C/402E (bus-extension bits), bit 1 passed to the emulator's disassembler as flag 40, bit 0 the run marker.
- **The DISP header** for the 8088 includes a Q column; that page 68 bit 4 holds the qualifier is **inferred**.
- **Fetch detection** for the disassembler: the pair at *4030 (8088 and 8085: mask 01, value 01) applied to page 69.

### Consecutive events (CONS)
- **Port 71 bits 7–4 = links:** bit 4 = 4→1, bit 5 = 1→2, bit 6 = 2→3, bit 7 = 3→4. The table at 225C (`41 12 412 23 _ 123 4123 34 341 _ 3412 234 2341 1234`) maps each chain to the bit pattern (index+1)<<4.
- **Port 71 bits 3–2 = what "consecutive" counts:** 00 `cyc`, 01 (04) `fet` (needs 40A7 bit 4), 10 (08) `emu` (needs 40A7 bit 6). 11 prints "internal error detected on I/O port 71".
- `cons clr` sets 71 &= 03. **Bit 0 of port 71 is never written by OS/40** (always preserved; the 8086 diagnostic sets it).

### Port usage summary (OS/40)

| Port | Bits that OS/40 drives |
|---|---|
| 70 | 0 comparators on (7325 sets, 7480 clears); 1 RAM select; 5–4 SRC5 select. Bit 2 (test) is never set |
| 71 | 7–4 cons links; 3–2 cons mode; 1 acquire all; 0 untouched |
| 72 | 3–0 break enables; 4 run marker; 5 set with acquire all; 7–6 untouched |
| 73 | written 00 at run start (7325); read with 72 at INT 29 |
| 74 | **never accessed by OS/40** |
| 75 | read by DISP (next sample index) |
| 76/77 | Am9513 |

## 4. Emulator-specific bus symbols
- **The tables come from the emulator support's resident EMUMON1 (loaded at 4008):**

  | Pointer | Contents |
  |---|---|
  | 4020/4022 | Y-byte keyword list and (mask, value) pairs, for `b=` |
  | 4024/4026 | DISP decode of page 69 |
  | 4028/402A | X-byte (bits 7–5) keyword list and pairs |
  | 402C/402E | DISP decode of page 68 |
  | 4030 | fetch pair |
  | 4034 | DISP header text |

  - **Pair order is (mask, value).** EVE 2418–243E ORs the first byte into the mask and the second into the value, and a symbol that conflicts with an earlier one sets an error flag.
- **8088** (160-1461; EMUMON1 at 5CFA/5D63, 5D84/5D95):
  - Y list: F(01,01) IA(0E,00) IR(0E,02) IW(0E,04) HT(0E,06) FS(0F,08) RD(0E,0A) WT(0E,0C) ES(30,30) SS(30,20) CS(30,10) DS(30,00) BHE(40,00) TST(80,00);
  - X list: INT(80,80) LCK(40,00);
  - 40A7 = 09, 40B4–40B7 = 0F FF FF 00.
- **8085** (DOS/50 V2.1 `VERSION2/EMUMON1`, file 0x106A; same layout):
  - Y list: NF(01,00) M(04,04) I(04,00) RD(02,02) WT(02,00) F(01,01);
  - X list empty;
  - DISP decodes page 69 as M/I, RD/WT, F;
  - fetch pair (01,01); 40A7 = 10 (`cons fet` allowed); 40B4–40B7 = 00 FF FF 00.
  - **So for the 8085, bus status bit 0 = fetch, bit 1 = read (0 = write), bit 2 = memory (0 = I/O).**
- **The Z80:** no OS/40 or DOS/50 V2 Z80 support is in the collection. TNIX lists the same generic `f nf m i rd wt` set for it, so the 8085 encoding is the natural assumption. **Inferred.**

## 5. Open
- **Port 74** (8086 diagnostic writes 75, TTA.TST walks it; OS/40 never touches it), **port 71 bit 0**, and **port 72 bits 5–7** (bit 5 always set together with 71 bit 1 by OS/40; bit 7 set by the 8086 diagnostic).
- **Bit 4 of pages 76–79** (never set by OS/40; F5 in TTA.TST).
- **Page 68 bits 4–1** in the record (qualifier? the 40 flag?).
- **Test-mode addressing** of the X/Y (page 70) and D15–8/probe nibble chips. The run-mode facts above don't settle what TTA.TST sees; the current test-mode model passes and should be kept.
- **Call sites of 7325 and 7480** (run start and stop) were not located; their role follows from their code.
- **What `cons fet`/`emu` and `emuclk` sample** in hardware, and the Z80 board's bus-status byte, which is assumed to match the 8085's.
