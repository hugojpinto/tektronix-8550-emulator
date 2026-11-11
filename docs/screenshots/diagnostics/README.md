# Disk-resident diagnostics on the emulated 8550

Both surviving diagnostic disks run Automatic System Verification to completion with zero errors. Both runs use `-pm ext`, which emulates the 1981+ program-memory boards that these diagnostics expect. They were captured before the Z80 emulator board became detectable, which is equivalent to today's `-ep none`. With the board fitted, the Z80 emulator test also runs; it passes except 07/00D0, the firmware PROM checksum, which needs the original PROM dump. Captured from the emulated CT8500 at its native 80×25 grid; `raw/` holds the bare 640×500 rasters.

| Disk | File | Shots |
|---|---|---|
| 062-5412-08, Diagnostic System 2.4 (1983) | `sources/bitsavers/disks/062-5412-08_8501_1err_DOS50.imd` | `d24_1_menu`, `d24_2_complete` |
| 062-5412-10, Diagnostic System 3.0 (1985) | `sources/bitsavers/disks/8550_DIAG.IMD` | `d30_1_menu`, `d30_2_8301_tests`, `d30_3_dmu_interaction`, `d30_4_complete` |

What runs, in about 990 emulated seconds (≈20 s wall clock):
- System Processor & I/O 1.4 (reports 2 MHz)
- Memory 1.4 (64K system + 64K program memory)
- Language Processor 1.2, including the Emulator Controller
- RTPA, correctly reported absent. With `-rtpa`, its diagnostic now runs and passes; see `../rtpa_tta/`
- DMU 1.2: RAM, ROM, CPU, LTC, AUX, HSI, flexible discs, and a 665-second device interaction test (the counters are octal)

Reproduce:
```
cd emu
build/tek8550 -ep none -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD -type 31 '\r' -type 41 '1\r' -type 51 '\r'
```
That types `<CR>`, `1<CR>`, `<CR>`, which selects Automatic mode → System Verification → display on terminal.

Without `-pm ext` (the default, needed by DOS/50 V1.0), Memory 3.0 sees 256K of aliased program memory and fails error 02/0002. That is the expected board-generation mismatch, not an emulator fault.
