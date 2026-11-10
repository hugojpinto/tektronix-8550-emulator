# Tektronix 8550 MDL — Emulation Feasibility Study

*2026-11-10. Sources: files in `inputs/`, bitsavers.org (`pdf/tektronix/85xx/`, `bits/Tektronix/`), TekWiki, vcfed thread 1259403. Everything downloaded is in `sources/`.*

## Verdict

**Yes, it is doable.** This is a moderate-sized project, not a research gamble. Every layer needed to boot DOS/50 and run its editor and assemblers survives: CPU types, boot ROMs, schematics, I/O port maps and bootable system floppies. The cross-target in-circuit *emulator* function (8085, Z80, 8086 and other pods) is the only part with real gaps, and it is not needed to run the host software.

## 1. What an 8550 actually is (corrects TekWiki and the forum)

The 8550 is **8301 MDU + 8501 DMU + CT8500 terminal**. It is not an 8002A; the 8002A is the previous generation, which ran TekDOS.

| Unit | CPU(s) | Role | Evidence |
|---|---|---|---|
| **8301** Microprocessor Development Unit | **Signetics 2650A-1** system processor (System Controller board, A20) | Runs DOS/50, editor, debug, I/O | 8301 Svc 070-2976-01 §1: "The system CPU, a 2650A-1 microprocessor…" |
| | **Z80A** Language Processor board | Slave to the 2650; runs the assemblers, linker and Pascal | same, §1 / §13 |
| | Optional emulator processor (8085, Z80, 8086…) + Emulator Controller board | Runs and debugs user target code | same, §12 |
| | 64K DRAM System RAM; 32–64K static Program Memory | | |
| **8501** Data Management Unit | **DEC LSI-11/2** (KD11-HA, real PDP-11) | File server for the 8301; two 8" DSDD drives | 8501 Svc 061-2355-00 §1: "uses a Digital Equipment Corporation LSI-11/2" |
| | **Z80A** Flexible Disc Controller | Disk formatting and low-level sector I/O | same, §1 |
| **CT8500** terminal | unknown, no ROMs dumped | Dumb/smart terminal on RS-232 | 070-3737-00 (escape codes are documented) |

- **Link between the units:** HSI, a synchronous/async serial link at 153.6 kbaud over RS-422. The 8301 also has an RS-232 terminal port, a remote port and an auxiliary port, all on 6850 ACIAs.
- **About "PDP-11-like" (vcfed, Al Kossow):** that is true of the 8540, the 8560 and the 8501 half of the 8550. The 8501 boot sector starts with `000240` (NOP), its programs are `.SAV` images, and the utility ROM disassembles to `MOV #77776,SP` and writes to the LTC CSR at `177546`. The 8301 side, where DOS/50 and the user live, is **2650 code**. You therefore need **two CPU emulations plus two Z80s**.

## 2. Inventory: what we have

### ROMs: curated set in `roms/` (see `roms/MANIFEST.md` for CRC32/SHA1)

| P/N | Unit / position | Content | Best dump |
|---|---|---|---|
| 160-0728-01 | 8301 System Controller U5040 | 2650 boot ROM "BOOT V2.1" (loads `/KERNL`, `8301/RESIDENT`) | `roms/8550/8301/160-0728-01.u5040`. Two independent bitsavers copies (`8301/firmware` and `68000/firmware/2650_cpu_bd_roms.zip`) are byte-identical |
| 160-0802-01 | 8301 System Controller U5030 | 2650 power-up diagnostics, 4 KB | `roms/8550/8301/160-0802-01.u5030` (likewise two identical copies) |
| 160-0399-01 / 160-0400-00 | 8501 Utility U5080 / U5090 | LSI-11 bootstrap + diags, low/high byte (82S2708, 1 KB) | your dumps, trimmed to the true 1 KB; the 2 KB files were mirrored. CRCs match TekWiki |
| 160-0412-00 | 8501 Floppy Controller U2030 | Z80 firmware, 4 KB mask ROM | **bitsavers 4 KB dump.** Your 2 KB file and TekWiki's CRC (c0e0af86) are the *upper half only*: the Z80 reset code (`LD SP,2400h` at 0000) and about 70% of call targets are in the missing lower half |
| 160-0884-00 (512×8, "I/O DECODE", photo-confirmed), 160-0822-00, 160-0727-00 (32×8) | 8301 A20 U5050, A30 U6100, A40 U2120 | bipolar decode PROMs | **not dumped**; their logic can be inferred from the schematics and port tables |

Other dumps, filed separately:
- `roms/8540/`: 8540 SIU system ROM board (160-1366 … 160-2201, plus 160-1401-01 "Comm option" and 160-1873-00 "Extended Hex option"). These belong to the 8540, not the 8550.
- `roms/emulator_processor/{68000,8088,80186}/`: emulator-board firmware, mostly from 8540 pods. The 68000 emulator-board PROMs 160-2023/2024/2025 may also apply to the 8550 68000 option.
- `roms/8560/`: the 8560/8562 set from `inputs/ROMs`. It is not needed for the 8550.

### Board photos (`sources/bitsavers/photos/`, about 5000 px, part numbers legible)
- **8301:** System Controller, System RAM 64K, 32K Program Memory, Emulation Controller, Language Processor (component side).
- **8501:** Utility, I/O and Floppy Controller boards (both sides).
- **Emulator processors:** 1802, Z80 "mobile processor".
- **System Controller, chips read off the photo:** 2650A-1, Intel P8257 DMA, an Intel 8251-family USART (probably the HSI at E8/E9), three 6850-type ACIAs, the 160-0884-00 I/O DECODE PROM, and the 2716/2732 boot/diag EPROMs.
- **8501 Floppy Controller, chips read off the photo:** MK3880 Z80, 160-0412-00 ROM, MK3801 STI, and **no FDC chip**. FM/MFM encode/decode is discrete TTL driven by the Z80.

### Disk images (all 77 cyl × 2 sides, 26 × 256-byte MFM sectors, track 0 side 0 = 26 × 128-byte FM: standard IBM 8" DSDD, about 1 MB)

| Image | Content | Read quality |
|---|---|---|
| `bits/Tektronix/8550/System.imd` | **DOS/50 V1.0 (18-AUG-80)** system disk: EDIT 4.0B, ASM V4.0B (8080/85, Z80, 6800), LINKER V4.0H, COMM V4.0, Pascal, 8501 V1.0 | clean |
| `bits/Tektronix/8550/8550_DIAG.IMD` | 062-5412-10 8550 system diagnostics V3.0 (1985) | clean |
| `85xx/8550/8301_8085-emul/062-5412-08_8501_1err_DOS50.imd` | Diagnostics 2.4 (files `8501.SAV`, `8301/RESIDENT`, `SYSPROC.SAV`, `LANGPROC.SAV`…) | 1 bad sector |
| `85xx/8550/8301_8085-emul/062-4641-03_8300E06_…imd` | 8085 emulator software V2.1 | clean |
| `bits/Tektronix/8550/8550_8086.IMD` | 062-5959-01 8086/88/87 emulator V1.15 | clean |
| `85xx/8500_MDL_Prom_Programmer/062-6036-01_…imd` | PROM programmer software 2.1 | clean |

I parsed all of them with `tools/imd.py`; raw dumps are in `sources/raw/`.

### Documentation (strong)
- **8301 Service, 070-2976-01** (532 pp, Jan 1984): theory of operation per board, I/O port tables (Table 7-2, 9-3, 12-2), interrupt vectors, memory-map/bank-switch registers, DMA, ACIA, HSI, **full schematics** (§18), and parts lists.
- **8501 Service, 061-2355-00** (356 pp): LSI-11/2 strapping, utility/I/O/comm boards, Z80 disc controller with registers, port addresses, ROM and disc diagnostics, schematics.
- In `inputs/`: user manuals for the Editor, Assembler core (A/B series), 8080/85 assembler specifics, ACE editor, Z80 emulator specifics, 64K/128K program memory service, PROM programmer, and the CT8500 operator's manual.
- Missing from bitsavers, but the parts list confirms they existed: DOS/50 System Users Manual (070-3457 V1 / 070-3936 V2), CT8500 Service 061-2431-00, 8085A Emulator Processor Service 070-2716-01, Emulator Controller detail beyond the 8301 manual.

## 3. 8301 I/O map (from Table 7-2, abridged)

| Port | Function |
|---|---|
| 90–98 | DMA controller (8257-style: ch0–2 address/count, control/status) |
| B8/B9 | Program-memory relocation, low/high board |
| CA/CB | Remote-port ACIA |
| CC/CD | Auxiliary-port ACIA |
| CE/CF | System-terminal ACIA |
| D2/D3 | System RAM diag/control; parity error address |
| E7 | Language Processor control/status |
| E8/E9 | HSI (to the 8501) data, control/status |
| EA/EB | Manufacturing test port; EA also gates the Boot ROM |
| EC | Interval timer / programmed reset |
| ED | LEDs (write) / switches (read) |
| EE | Bank switch |
| EF | Sync test |

That is a small device set, which is good news.

**8501 (LSI-11, octal), from 061-2355-00:**

| Address | Function |
|---|---|
| 177160 | floppy controller command buffer (Z80 FDC) |
| 177560 | console/AUX port, vector 60 |
| 177510 | printer port, vector 200 |
| 176510 | HSI to the 8301, vector 360 |
| 177546 | line-time clock |
| 173000–174776 | boot/diagnostic PROM |

The manual's memory map labels a "File Manager (DOS/50)" in the 8501. DOS/50 is therefore split across both CPUs: the 2650 runs the user-facing monitor and the LSI-11 runs the file manager.

**Prior art:** the earlier 8002A uses the same 2650 system-processor plus Z80 design and is internally the Signetics TWIN / Millennium Universal One (see ztpe.nl/2650/one-computer-four-microprocessors/). TWIN knowledge carries over to the 8301. **No software emulator exists for any 8001, 8002, 85xx or CT8500 machine.** MAME's `src/mame/tektronix/` has none, and GitHub turns up nothing.

## 4. Building blocks: MAME (checked against mamedev/mame master, 2026-11)

| 8550 part | MAME component | Notes |
|---|---|---|
| 2650A-1 (8301) | `src/devices/cpu/s2650` (+ disassembler) | Mature: used by about 30 drivers (arcadia, vc4000, cvs, zaccaria, instruct, pipbug, binbug…) |
| LSI-11/2 (8501) | `src/devices/cpu/t11`, **`LSI11` device type** | Source says *not implemented: instruction timing, microcode insns, HALT state and **ODT**, double-bus-error traps*. The 8501 uses ODT for its maintenance console, so that needs adding (a few hundred lines) |
| Q-bus | `src/devices/bus/qbus/` | Exists; Tek's 8501 cards are non-standard, so they will be custom devices |
| Z80A ×2 | `src/devices/cpu/z80` | Trivial |
| MK3801 STI | `machine/z80sti` | Exists |
| 8257 DMA, 8251 USART, 6850 ACIA | `machine/i8257`, `i8251`, `6850acia` | All exist |
| IMD floppy images | `lib/formats/imd_dsk.cpp` | Our images load as they are |
| Tektronix | `src/mame/tektronix/` | 405x, 410x, 43xx, 440x, MSU FDC, xd88… **no 8001/8002/85xx/CT8500** |

SIMH's PDP-11 (`SET CPU 11/03`) is the reference for LSI-11 behaviour, ODT included, if MAME's core disagrees.

## 5. Risks and unknowns

| Risk | Severity | Mitigation |
|---|---|---|
| Tek-custom LSI-11 peripherals in the 8501 (Al Kossow: "incompatible with normal I/O programming") | Medium | Fully described in the 8501 service manual; `8501.SAV` and the utility ROM show exactly which CSRs are used |
| 8501 floppy controller is **discrete TTL** (no FDC chip) driven by a Z80 | **Medium–High** if done at gate level | Recommended: **high-level emulation** of the LSI-11-facing interface. It is a command buffer at 177160 with a pointer to a DMA command block, documented in 061-2355-00 §1/§7. Run the real Z80 ROM later, against a modelled TTL data path, only if needed |
| HSI link protocol 8301↔8501 | Low | We emulate both ends at instruction level, so only the UART/USRT hardware is needed, not the protocol |
| Undumped bipolar PROMs (decoders) on 8301 boards | Low–Medium | Their function follows from the schematics and port tables; emulate behaviourally |
| **ROM/OS version match:** the boot ROM is BOOT V2.1 (`/KERNL`) while System.imd is **DOS/50 V1.0** | **Medium** | The diagnostics disks (`8301/RESIDENT`) are the first boot target. Whether V2.1 ROM boots V1.0 must be checked by parsing the DOS/50 directory. A DOS/50 **V2** system disk would be the ideal find |
| Emulator-processor pods (8085/Z80/8086) | High, but optional | Disks survive; board service manuals are mostly missing. Do a "mode 0" style software-only target emulation later |
| CT8500 firmware not dumped | Low | Not needed; ACE (the full-screen editor) needs its escape set, which is documented |
| OCR quality of manuals | Low | Text layers exist; schematics are readable images |

## 6. Suggested roadmap

1. **Inventory and tooling (days).** Write a DOS/50 filesystem lister/extractor from the raw images, using the 8501 manual and `8501.SAV`. Disassemble the 2650 boot ROM, the LSI-11 utility ROM and the Z80 floppy ROM.
2. **8501 alone (weeks).** LSI-11/2 + utility board + Z80 disc controller + drives. Milestone: the ROM power-up tests pass, ODT works on the console port, and `8501.SAV` boots from System.imd.
3. **8301 alone (weeks).** 2650 + System RAM + bank switching + ACIAs + interval timer + DMA. Milestone: the diagnostics ROM 160-0802-01 runs and reports on the terminal port.
4. **Couple them over HSI.** Milestone: BOOT V2.1 loads `8301/RESIDENT` from the diag disk, then DOS/50 from the system disk, and you reach a prompt.
5. **Language Processor Z80.** Milestone: run ASM/LINK for 8080/Z80 under DOS/50.
6. **Optional:** CT8500 emulation for ACE; 8085/Z80 emulator processor and Emulator Controller breakpoint logic; PROM programmer.

A rough estimate for one experienced emulator developer is **2–4 months to a DOS/50 prompt with working assembler**. Most of the uncertainty is in steps 2 and 4.

## 7. Things worth hunting for
- A **DOS/50 V2.x system disk** (part 062-5xxx) and the **DOS/50 System Users Manual** 070-3457 / 070-3936.
- **CT8500 service manual** 061-2431-00 and ROM dumps.
- Dumps of the 8301 bipolar PROMs 160-0884-00, 160-0822-00 and 160-0727-00.
- alt.folklore.computers, Oct 1993, "Tektronix 8550 system?": a rescued unit with the same LSI-11/2, Z80 and 2650A layout.
- The vcfed owner (thread 1259403) has an 8501 + 8540s + 8560. Floppies or PROM dumps from them would help. Al Kossow (bitsavers) has run an 8560 in the past.
