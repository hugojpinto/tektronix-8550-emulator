# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
"""Approximate CT8500 character set on its documented 6x8 dot matrix (8x10 field).

The CT8500 character ROM has never been dumped. This font combines:
  * 20h-5Fh (space, digits, punctuation, upper case): glyphs from the Tektronix 308
    data analyzer character generator 160-0797-00 (c. 1980, roms/other/308/), the
    closest surviving Tek character ROM of the period. Its 5x7 glyphs sit on rows 1-7
    of each 8-row cell; here they are moved up one row so row 7 is free for descenders.
  * 60h-7Eh (lower case, ` { | } ~): drawn for this emulator in the same 5-wide stroke
    style -- x-height rows 2-6, ascenders from row 0, descenders (g j p q y) in row 7.
glyph(code) -> list of 8 row values, bit 4 = leftmost of 5 dots.
"""
import os

_ROM = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'roms', 'other', '308', '160-0797-00.u486')
_rom = open(_ROM, 'rb').read()

def _from_308(c):
    i = 0x80 + (c - 0x40) if c >= 0x40 else 0xa0 + (c - 0x20)   # 308 ROM order: @..._ then space..?
    rows = [b & 0x1f for b in _rom[i * 8:i * 8 + 8]]
    return rows[1:] + [0]                                          # shift up: caps on rows 0-6

_LOWER = {
'`': ".#... ..#.. ...#. ..... ..... ..... ..... .....",
'a': "..... ..... .###. ....# .#### #...# .#### .....",
'b': "#.... #.... #.##. ##..# #...# #...# ####. .....",
'c': "..... ..... .###. #.... #.... #...# .###. .....",
'd': "....# ....# .##.# #..## #...# #...# .#### .....",
'e': "..... ..... .###. #...# ##### #.... .###. .....",
'f': "..##. .#..# .#... ###.. .#... .#... .#... .....",
'g': "..... ..... .#### #...# #...# .#### ....# .###.",
'h': "#.... #.... #.##. ##..# #...# #...# #...# .....",
'i': "..#.. ..... .##.. ..#.. ..#.. ..#.. .###. .....",
'j': "...#. ..... ..##. ...#. ...#. ...#. #..#. .##..",
'k': "#.... #.... #..#. #.#.. ##... #.#.. #..#. .....",
'l': ".##.. ..#.. ..#.. ..#.. ..#.. ..#.. .###. .....",
'm': "..... ..... ##.#. #.#.# #.#.# #.#.# #.#.# .....",
'n': "..... ..... #.##. ##..# #...# #...# #...# .....",
'o': "..... ..... .###. #...# #...# #...# .###. .....",
'p': "..... ..... ####. #...# #...# ####. #.... #....",
'q': "..... ..... .#### #...# #...# .#### ....# ....#",
'r': "..... ..... #.##. ##..# #.... #.... #.... .....",
's': "..... ..... .#### #.... .###. ....# ####. .....",
't': ".#... .#... ###.. .#... .#... .#..# ..##. .....",
'u': "..... ..... #...# #...# #...# #..## .##.# .....",
'v': "..... ..... #...# #...# #...# .#.#. ..#.. .....",
'w': "..... ..... #...# #...# #.#.# #.#.# .#.#. .....",
'x': "..... ..... #...# .#.#. ..#.. .#.#. #...# .....",
'y': "..... ..... #...# #...# #...# .#### ....# .###.",
'z': "..... ..... ##### ...#. ..#.. .#... ##### .....",
'{': "...## ..#.. ..#.. .#... ..#.. ..#.. ...## .....",
'|': "..#.. ..#.. ..#.. ..#.. ..#.. ..#.. ..#.. .....",
'}': "##... ..#.. ..#.. ...#. ..#.. ..#.. ##... .....",
'~': "..... ..... .#... #.#.# ...#. ..... ..... .....",
}

def _parse(s):
    return [int(r.replace('#', '1').replace('.', '0'), 2) for r in s.split()]

def glyph(code):
    if 0x20 <= code <= 0x5f: return _from_308(code)
    ch = chr(code)
    if ch in _LOWER: return _parse(_LOWER[ch])
    return _from_308(0x3f)                                         # '?'
