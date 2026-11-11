# The host-computer link (8301 REMOTE port)

On a real 8550 the 8301's **REMOTE** port (J102, the third ACIA, at CA/CB, interrupt INT_REMOTE) connects to a host computer or to another Tektronix lab over RS-232. Two DOS/50 commands use it:

- **`COMM`** (COMM V4.0) turns the 8550 into a terminal on the host. A NUL (Ctrl-@) starts an 8550-side transfer directive.
- **`HOST`** (Host V2.3) makes the 8550 the host for a lab running COM/COMM: an 8540, an 8002A, or another 8550.

In the emulator the port is a byte stream over a socket: `tek8550 -remote tcp:HOST:PORT | listen:PORT | unix:PATH`. `-remote-baud N` sets the rear-panel REMOTE BAUD switch (default 2400). The reference host is `tools/host_link.py`, and `tools/test_host_link.sh` is the regression.

Sources:
- the 8540 System Users Manual (`sources/bitsavers/manuals/8540/os40.txt`), sections "Intersystem Communication" and "8540/8550 Interface";
- DOS/50's own COMM overlays (`8301/<chip>/COMM.PARM`, `.DNLD`, `.UPLD`, `.RIOT`; 2650 code loaded at 6400), traced in the emulator.

No 8550 System Users Manual is available.

## COMM: syntax and directives
`COMM [E=R|L] [L=O|I] [P=prompt] [T=delay] [M=parity] [C=T|I]`, as in the 8540's COM:
- `P` is the host's prompt in hex, e.g. `P=2420` for "$ ";
- `E=R` (the default) means the host echoes what is typed.

What is typed goes to the host until a NUL. What follows the NUL, up to CR, is for the 8550:

| Typed on the 8550 | Transfer |
|---|---|
| `host-command` NUL CR | formatted download: Tekhex from the host into program memory |
| `host-command` NUL `lo hi [lo hi…] [transfer]` CR | formatted upload: memory to Tekhex on the host |
| `host-command` NUL `>dest` CR | unformatted ASCII download into a DOS/50 file or device (ends at the host's prompt) |
| `host-command` NUL `<src` CR | unformatted ASCII upload of a DOS/50 file or device, one line per prompt. COMM shows `RIOT:` at the end, and the user types the host's end-of-file |
| NUL ESC | leave COMM |

DOS/50 V1 handles Standard Tekhex only:
- data block: `/AAAACCSS` + data + `SS`, with nibble-sum checksums, at most 30 bytes per block;
- termination block: `/TTTT00SS`;
- abort block: `//message`.

Each block prints `*BLOCK RECEIVED AT: addr count` or `*BLOCK SENT AT: addr`.

## Protocol details established in the emulator
- **Handshake.** A formatted download goes: block, CR, prompt. COMM then answers ACK `0` or NAK `7`, followed by CR. COMM waits for the prompt before checking a block. An upload is the mirror image.
- **COMM discards the echo of every line it sends.** After the command line, and after each ACK/NAK + CR, it skips input up to the next CR (routines 6330 and 6339 in COMM). That is the host's echo under `E=R`. A host that doesn't echo the ACK loses its next block, and the transfer then stalls on the termination block. `host_link.py` echoes everything; use `--no-echo` with `COMM E=L`.
- **Fill characters.** The host should send a few fill characters (spaces) before each block while COMM finishes the previous one. The manual suggests 4–6 at 2400 baud; `host_link.py` sends 8.
- **Termination block.** COMM ACKs it, pauses, prints `DNLOAD:` and returns to terminal mode; COMM stays active until NUL ESC.
- **HOST expects human typing.** HOST reads the port under interrupt and echoes each character, about 15 ms per echo. A request line arriving faster than that, at full line speed from a script, loses characters. The baud rate matters too: at the emulator's original ~9600 baud the filespec was truncated to 8 characters, and at 2400 to 10. Typed at human speed it works.
- **HOST conversation:**
  - HOST prints `Host V2.3.` and sends `Enter U or D, A, B or H, and 8550 filespec:`;
  - after the request, it sends `Enter transfer sequence, beginning with null character:`;
  - it sends the Tekhex blocks, then reports `HOST: Error FF End of file`. The 8540 manual says that message appears after every successful transfer.

## Verified
- **`tools/test_host_link.sh`** (COMM against `host_link.py`): formatted download into memory (checked with `D`), formatted upload to a Tekhex file, ASCII download into a DOS/50 file (checked with `COPY … CONO`), ASCII upload of `MAKE.KONG`. 7 checks.
- **Two emulated labs:** `tek8550 -remote listen:P` running HOST, and `tek8550 -remote tcp:127.0.0.1:P` running `COMM P=7E7E` with `DHHELLO.HEX` typed at 0.25 s per key. Lab B received the three blocks into program memory, and lab A ended with `Error FF End of file`.

## Not modelled
- The modem-control lines (DCD/CTS always asserted) and the MODE SELECT switch.
- Parity and framing errors: the line is 8-bit clean.
- ACIA overruns: received bytes wait until the program reads them. Losses like the HOST one above happen in DOS/50's own buffering.
- The other ports take the same kinds of peer: `-aux8301`, `-aux8501` and `-printer` (see `notes/8501.md` for the 8501 ports' modem lines and the printer vector). The 8301 diagnostic's display option 2 ("DISPLAY ON TERMINAL & J103 AUX PORT PRINTER") sends its log to the 8301 AUX port.
