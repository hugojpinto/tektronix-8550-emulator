# RTPA and TTA on the emulated 8550 (2026-11-11)

Captured from the emulated CT8500 at its native 80×25 grid. `raw/` holds the bare 640×500 rasters. All runs use `8550_DIAG.IMD` (Diagnostic System 3.0) with `-pm ext`, or the DOS/50 system disc.

| Shot | What it shows |
|---|---|
| `01_rtpa_diag_before_absent` | RTPA diagnostic V1.2 without the option: "NO RTPA BOARD DETECTED", which was every run before today |
| `02_rtpa_diag_option0_pass` | `-rtpa`, option 0 (all 16 event tests and the counter test): iterating with ERRORS = 0000 |
| `03_rtpa_diag_option7_probe_pass` | `-rtpa probe`, option 7: the probe setup instructions, then clean iterations |
| `04_asv_with_rtpa` | Automatic System Verification with `-rtpa`: the RTPA diagnostic now runs, with 0 errors, between the Language Processor and the DMU tests. The run ends in SYSTEM VERIFICATION COMPLETE |
| `05_dos50_rtpa_setup` | DOS/50 V1.0: event 1 = fetch at 0105; event 2 = fetch at 0106 with pass count 3; break on event 2; count fetches; trace fetches |
| `06_dos50_rtpa_break_drt` | `GO 100` breaks on the third pass (TRIG2 at 0106). `CNT` = 13 fetches (1 + 3×4), and `DRT` lists the traced fetches with mnemonics |
| `07_tta_diag_modules_pass` | `-tta`, TTA diagnostic V1.3A: detection, I/O ports and RAM modules (00–23) running with ERRORS = 0000 |
| `08_tta_diag_events_not_yet` | The TTA's limit today: module 30/31 (event comparators) fails. See `notes/tta.md` |

Reproduce (from `emu/`):
```
build/tek8550 -rtpa -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD -type 31 '2\r' -type 41 '4\r' -type 51 '0\r' -type 56 '3\r' -type 61 '\r'
build/tek8550 -rtpa probe -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD -type 31 '2\r' -type 41 '4\r' -type 51 '7\r' -type 56 '3\r' -type 61 '\r' -type 66 '\r'
build/tek8550 -rtpa -ep none -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD -type 31 '\r' -type 41 '1\r' -type 51 '\r'
build/tek8550 -tta -pm ext -0 ../sources/bitsavers/disks/8550_DIAG.IMD -type 31 '2\r' -type 41 '6\r' -type 51 '\r' -type 56 '3\r' -type 61 '\r' -type 66 '\r'
```
