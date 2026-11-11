// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// What OS/40's EVE writes into the TTA's comparator RAMs for one event (ROM 160-1381;
// notes/tta-os40.md, "The codes OS/40 writes"). Every RAM row holds, for the input byte that
// addresses it, a small code; the board combines them into the event (tta::comparators):
//   ADDR = M7 & !(M6 & (m3 | m2 & p1)) & !(M5 & (m1 | m0 & p0)),
//   DATA = !n2 & !(n1 & q1) & !(n0 & q0).
// tests/tta_eve.cpp checks every kind of comparison against that function.
#ifndef TEK_SYSTEM_TTA_EVE_H
#define TEK_SYSTEM_TTA_EVE_H
#include "emu.h"
#include <functional>

// a value, a value with don't-care bits, or an inclusive range; optionally excluded
struct tta_field {
	bool on = false, negate = false, masked = false;
	u32 lo = 0, hi = 0, care = 0;              // masked: (x & care) == (lo & care)
};

struct tta_eve {
	tta_field addr, data;
	u8 pm = 0, pv = 0;                         // probe: (P & pm) == pv
	u8 xm = 0, xv = 0;                         // X byte: bus extension 7-5, qualifier 4, OUT4-1 3-0
	u8 ym = 0, yv = 0;                         // Y byte: the bus status
	// the emulator's widths (its EMUMON1, 40B4 and 40B7): the A23-16 row mask (00 for the
	// 16-bit-address emulators), and a 16-bit data bus
	u8 top_mask = 0x00;
	bool data_wide = false;
};

// write event k (0-3) into the comparator pages (70-74, 76-79), keeping the other events' bits
void tta_eve_write(int k, const tta_eve &e, const std::function<u8(int page, int row)> &rd,
	const std::function<void(int page, int row, u8 v)> &wr);
#endif
