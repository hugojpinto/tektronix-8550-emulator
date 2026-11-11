# Working disk images

Raw linear images: 3991 records × 256 bytes, in 8501 record order (record N at offset N·256).
`tek8550` / `tek8501` accept these as well as `.imd`. Originals stay untouched in `sources/bitsavers/disks/`.

| File | Derived from | Change |
|---|---|---|
| `System_helpfix.img` | `System.imd` (DOS/50 V1.0) | Records 3854–3855 replaced with records 1474–1475. The HELP file's second copy had two blocks overwritten by `TERM.OB`/`TERM1.OB` data ("Error 06 Short or unterminated read" after SEArch). Records 3848–3853 and 3856–3857 match 1468–1473 / 1476–1477 byte for byte, so the restored text is a confident reconstruction. `TERM.OB` / `TERM1.OB` themselves are probably damaged. |

Rebuild it any time with `tools/make_system_helpfix.py` (byte-identical to the image first committed in feefc65). Don't run sessions on it with `-rw`: copy it first (`cp disks/System_helpfix.img /tmp/work.img`), so experiments never end up in the reference image.
