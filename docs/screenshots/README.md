# 8550 session: edit, assemble, link, run (emulation mode 1, external target)

Captured from the emulated CT8500 at its native 80×25 grid (10×20 px cells, 800×500 px).
Regenerate with `tools/make_screenshots.sh`.

1. ![boot](01_boot.png) Power-on: 8301 boot ROM, 8501 file server, **DOS/50 V1.0** prompt.
2. ![ace new](02_ace_new.png) `SEL Z80`, `ACE HELLO.ASM`: a new, empty file in the full-screen editor.
3. ![ace edited](03_ace_edited.png) After Insert Line (CT8500 key code ESC 0C) and typing the program; `EX` saves it.
4. ![asm](04_asm.png) `ASM HELLO.OBJ HELLO.LST HELLO.ASM`: Tektronix Z80 ASM V4.0B, no errors.
5. ![listing](05_listing.png) `COPY HELLO.LST CONO`: the listing with object code and symbol table.
6. ![link](06_link.png) `LINK HELLO.LOAD HELLO.LNKL HELLO.OBJ`: transfer address 0100.
7. ![run](07_run_mode1.png) `EM 1`, `LOAD`, `GO`: the program runs on the Z80 emulator processor; ESC ESC breaks it; `DS` shows the registers.
8. ![target](08_target_console.png) Meanwhile, on the target system's own console (`tools/proto_target.py`, reached over IPC): the program's `OUT (10H),A` output.
