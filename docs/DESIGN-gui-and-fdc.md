# Design: low-level floppy controller, and graphical front ends

Status: proposal (2026-11-11). Nothing here is implemented yet.

Two pieces of work share one prerequisite: the full-system main loop must stop
being a command-line program and become a reusable *system*. Both the floppy
controller and the GUI become simpler after that refactor.

---

## 0. The prerequisite: a `tek8550` system class

Today `emu/src/main8550.cpp` does five jobs: it parses options, wires the two
units together (the HSI cable, the target, REMOTE), schedules scripted events
(`-type`, `-snap`, `-poke`), paces the run to real time, and drives the host
terminal. Only the last two depend on the front end.

Split it like a MAME driver:

```
emu/src/machine/   tek8301, tek8501, rtpa, tta, ...         (unchanged: no host deps)
emu/src/system/    tek8550.{h,cpp}      the whole lab: owns both units, the HSI cable,
                                         the scripted-event queue, named outputs/inputs
                   options.{h,cpp}      the command line, shared by every front end
emu/src/term/      ct8500.{h,cpp}       terminal *model* only (cells, attributes, cursor, keys)
emu/src/frontend/  tty/                 today's behaviour: ANSI renderer, raw tty, monitor
                   sdl/                 new: SDL3 windows (optional build)
```

The system has no notion of wall-clock time:

```cpp
class tek8550 {
public:
    bool configure(const options &o, std::string &err);   // ROMs, discs, boards
    void run_for(double emulated_seconds);                 // lockstep slices, scripted events
    double now() const;                                    // emulated seconds
    ct8500 &terminal();                                    // the CT8500 on the system port
    outputs &out();                                        // named lamps (see below)
    void input(std::string_view name, int value);          // buttons, switches, door
    bool mount(int drive, const std::string &path, std::string &err);
    void eject(int drive);
};
```

Each front end owns pacing. The tty front end runs flat out under batch and paces
to real time when interactive. The SDL front end runs `run_for(frame_dt)` once per
video frame, with a turbo key that runs flat out. Determinism is unchanged: the
emulation is a function of (inputs, emulated time) whichever front end drives it.

**Acceptance:** a pure refactor. `tek8550` output, the screenshots and every
regression check stay byte-identical.

### Named outputs and inputs (the MAME way)

The machines publish lamps as named outputs at the point where the hardware
latches them. This mirrors MAME's `output_finder`, so it carries straight over
when the code is upstreamed:

| Output | Source |
|---|---|
| `8301:led0..4`, `8301:selftest` | port ED (and D2 D3–7) |
| `8501:led0..4` | LTC<15:11>, active low |
| `fdc:led0..2` | the three LEDs on the controller board (once mapped) |
| `drive0..3:activity`, `drive0..3:head` | drive select / head load |
| `ct8500:bell` | BEL |

Inputs use the same scheme: `8301:restart`, `8501:restart`, `8301:modesw`
(DIP), `drive1:door`.

The headless build loses nothing. It ignores outputs by default, and gains
`-outputs FILE` (a timestamped change log) plus monitor commands
(`press 8501:restart`, `mount 1 x.img`). Panel behaviour can then be checked in
batch runs, the same way the screens are checked today.

---

## 1. Floppy controller: from HLE to the real board

### What the board is (established)
- **Service manual 061-2355-00, §12 and Fig. 1-9:** the Flexible Disc Controller
  has a Z80A, 1 KB of RAM, 4 KB of ROM and a disc drive interface built from TTL
  (FM/MFM encode, write precompensation from track 43, data separation, CRC).
  There is no FDC chip. The Z80 runs the LSI-11 command-block protocol itself and
  moves data by DMA.
- **Parts list:** A50 is the "SD FLEX CONTROLLER" 670-6228-00, and **A50U2030 is
  160-0412-00, a 4096×8 ROM.** That is `roms/8550/8501/160-0412-00.u2030`, which
  is already in our set and unused.
- **Board photo** (`sources/bitsavers/photos/8501/8550_Floppy_T.jpg`): MK3880P-4
  (Z80A) at U2020, the 160-0412-00 ROM at U2030, MK4801AP-4 (1K×8 SRAM) at U2040,
  an MC4024 VCO (the PLL data separator), and three LEDs.
- **First look at the ROM:** `LD SP,2400h` at reset, so RAM is at 2000–23FF.
  Memory-mapped registers sit at 4000, 6000, 8000, A000, C000 and E000–E4xx
  (the LSI-11 side: the control buffer and DMA window). The ROM uses IM 1, with
  the interrupt raised by the LSI-11's write to CBPR. Fig. 1-11 names the registers
  to match: HCR0/HCR1 (drive select, step, side, density, precomp),
  HSR0/HSR1 (TRK00, ready, index, disc change, WP, CRC error, data detected),
  DWDR/DRDR (disc write/read data), DTR/DRR, DMAAR and CBPR.

The firmware also implements the documented diagnostic command 63, which loads
and runs code in Z80 RAM at 2080. The HLE can never do that; the real board does.

### Reverse-engineering steps (the project's usual method)
1. Disassemble 160-0412-00 (`build/dasm z80 …`, `notes/dasm/8501_fdc.lst`). Map
   each memory-mapped register to Fig. 1-11 from how the code uses it: polling
   loops, bit tests, the step and seek routines, the format table.
2. Run the 8501 diagnostics' **Test 5 (FDC)** and DOS/50 against a logging stub
   board, as was done for the RTPA and TTA.
3. Record the facts in `notes/8501-fdc.md`, with how each was established.

### Emulation model
- `machine/tek8501_fdc.{h,cpp}`: one class for the board, MAME-device style.
  A second superzazu Z80 at 4 MHz, the 1 KB RAM, the ROM, and the Q-bus interface
  as a **bus master** (DMA through the `tek8501` bus, with NXM giving the DMA
  timeout). It runs in lockstep inside `tek8501::run_slice`.
- `machine/qume_dt8.{h,cpp}`: the Qume DataTrak 8 drive. 360 rpm (index every
  166.7 ms), step rate and head settle, head load, TRK00, ready, door and disc
  change, write protect, two sides.
- **Byte-cell tracks, not flux.** The Z80 never sees bits: the TTL serialises
  bytes, finds the sync/address marks and computes the CRC. Each track is held as
  a sequence of (byte, is-mark) cells, timed by density: 32 µs/byte FM and
  16 µs/byte MFM at 8″ rates, so 64 Z80 cycles per MFM byte. Reading DRDR before
  the next byte is ready stalls the Z80 (WAIT), which is how such boards pace a
  CPU to the disc. This model is exact at the level the Z80 can observe, and much
  cheaper than flux. The drive model sits behind an interface, so a MAME
  `floppy_image` flux backend can replace it when upstreaming.
- **Images:** IMD and raw are expanded at mount into IBM 3740 / System 34 track
  layouts (gaps, marks, CRCs; track 0 side 0 FM with 128-byte sectors, the rest
  MFM with 256-byte sectors, as `notes/8501.md` records). They are written back by
  decoding the tracks. A blank image becomes an *unformatted* disc, as on the real
  drive, so FORMAT really lays down tracks. A track that can't be expressed as
  sectors (only possible through diagnostics) is reported on save, not lost
  silently.
- The HLE stays available as **`-fdc hle`**, for speed and A/B comparison.

### Consequences to plan for
- Real seek and rotation times move the DOS/50 prompt later than 24 s. The
  `-type` times in `make_screenshots.sh`, CLAUDE.md and the regression commands
  need retiming once, and the screenshots change once on purpose.
- **New acceptance tests:** the ROM boot, 8501 diagnostics Test 5 including
  command 63, FORMAT in all four geometries (checked with `../tekmato check`), and
  every existing regression check. Known-gap entries to drop afterwards: "no
  timing, no media errors or retries, a blank image reads as formatted".

---

## 2. Graphical front end (SDL3)

### Principles
1. **The core never includes SDL.** `machine/`, `system/` and `term/` build and
   test with no dependencies. The GUI is one more consumer of `tek8550`, like the
   tty front end.
2. **One binary per front end, same options:** `build/tek8550` (headless/tty,
   always built) and `build/tek8550-gui` (built by `make gui` when
   `pkg-config sdl3` finds SDL3). Every scripted option (`-type`, `-snap`,
   `-cycles`, …) works in both, because both live in `system/`.
3. **Single-threaded:** poll events, run `run_for(dt)`, render. SDL wants events
   on the main thread on macOS anyway, and one thread keeps runs deterministic and
   snapshots exact.

### CT8500 window
- **Model/renderer split:** `ct8500` keeps the cells, attributes, cursor, pages
  and modes, and exposes them read-only (`cell(r,c)`, `attr(r,c)`, `cursor()`,
  `top()`). Its ANSI `render()` moves into `frontend/tty/`. The `key()` API takes
  a CT8500 key enum; the ANSI-sequence parser maps onto it in tty, and SDL key
  events map onto it in the GUI.
- **Faithful raster:** the manual gives a 6×8 dot matrix in an 8×10 field, green
  on black, and five attributes (blink, underline, inverse, dim, blank). The
  native raster is 80×8 by 25×10 = 640×250 dots, drawn from the same font as the
  screenshots: `tools/ct8500_font.py` turned into a generated
  `term/ct8500_font.inc` (Tek 308 ROM upper case plus our lower case, which stays
  labelled as an approximation until the CT8500 ROM turns up).
- **Presentation:** the 640×250 frame is scaled to a 4:3 tube with options for
  sharp pixels or a simple CRT look (green phosphor, scanline gap, slight glow),
  done on the CPU into an SDL texture and only when the screen changes. Plain
  `SDL_Renderer`, with no shaders, for maximum portability. Blink and cursor follow
  the terminal's own rates. BEL plays a short beep through SDL audio.
- **Typing:** SDL text input gives the printable characters (and honours the host
  keyboard layout). Key events give Return, Backspace, Ctrl-letters and the
  CT8500 function keys (mapped from the manual's keyboard section). Emulator
  hotkeys use a modifier the terminal never needs (Cmd on macOS, Ctrl+Alt
  elsewhere): paste, copy the screen as text, turbo, full screen, reset.
- **Paste types at the line rate**, into the model's `send` path, so long programs
  can be pasted the way a real host link would deliver them.

### Front-panel windows
- **Data-driven, MAME-style layouts.** Each unit's panel is a layout file
  (`emu/artwork/8301.lay`, `8501.lay`) in a subset of MAME's `.lay` XML: elements
  (rect, disk, text, image), LEDs bound to output names, buttons bound to input
  names. The GUI binds these generically to `outputs`/`input()`. The same files
  can go into MAME's `layout/` directory when upstreaming. XML is parsed with
  pugixml (MIT, already in MAME's 3rdparty), linked only into the GUI.
- **8301 panel:** power, the SELF TEST and status LEDs (port ED), RESTART, and the
  mode DIP switch.
- **8501 panel:** the five LTC LEDs, RESTART, and **two drive bays**. Each bay shows
  the activity LED, door state and the mounted image's label (read with the
  tekmato format rules). Drop an image on a bay to mount it; the door can be
  opened to eject (that raises *disc change*, which the low-level controller
  reports for real).
- There are no front-panel photos in `sources/` yet. The panels are drawn from
  the manual's descriptions until real artwork is found (tekwiki or bitsavers);
  layout files make swapping artwork a data change, not a code change.
- **Windows:** one SDL window per device (terminal, 8301, 8501), because they are
  three boxes on a bench, with a `-gui single` option that composes them into one
  window for small screens.

### Portability
- macOS and Linux: SDL3 from Homebrew or the distribution, same Makefile.
- Windows: MSYS2/MinGW with the same Makefile. The GUI binary does not need
  `tty.h`. `ipc_target`/`serial_link` need a small socket shim (Winsock) behind
  `host/net.h`. A CMake file is added only if an MSVC build is wanted.

---

## 3. Suggested order
1. **System refactor** (§0) plus outputs and inputs, kept byte-identical. Small; unblocks both.
2. **FDC reverse engineering and low-level emulation** (§1), behind `-fdc lle`
   until it passes everything, then made the default. Retime the scripts.
3. **CT8500 SDL window** (§2): the model split, raster, typing, paste.
4. **Front panels** from layout files; the drive bays use the FDC's real drive
   lamps and disc-change signal.
