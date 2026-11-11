// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA's comparators: OS/40's EVE encodings (system/tta_eve) written into the board's RAMs
// must make tta::comparators answer exactly the comparison each one stands for -- a value, a
// range, a range excluded (an=/dn=), a value with don't-care bits, or nothing -- for 24-bit
// addresses and 8- and 16-bit data, including bounds that share their upper bytes. This checks
// the inferred decision function against every encoding (notes/tta-os40.md).
#include "machine/tta.h"
#include "system/tta_eve.h"
#include <cstdio>
#include <random>

static int failures = 0;

static bool field_match(const tta_field &f, u32 v)
{
	if (!f.on) return true;
	if (f.masked) return ((v ^ f.lo) & f.care) == 0;
	const bool in = v >= f.lo && v <= f.hi;
	return f.negate ? !in : in;
}

static tta_field random_field(std::mt19937 &r, int bits)
{
	const u32 full = (1u << bits) - 1;
	tta_field f;
	switch (r() % 5) {
	case 0: return f;                                   // clr
	case 1: f.on = true; f.lo = f.hi = r() & full; return f;
	case 2: case 3: {                                   // a range or its exclusion, often sharing upper bytes
		f.on = true; f.negate = (r() & 1) != 0;
		u32 a = r() & full, b = r() & full;
		const int share = int(r() % 3);                 // 0, 1 or 2 upper bytes in common
		const u32 keep = share == 0 || bits <= 8 ? 0 : (full & ~((1u << (bits - 8 * share)) - 1));
		b = (a & keep) | (b & ~keep);
		if (a > b) std::swap(a, b);
		f.lo = a; f.hi = b; return f;
	}
	default:
		f.on = true; f.masked = true; f.care = r() & full; f.lo = r() & f.care; return f;
	}
}

static u32 near(std::mt19937 &r, const tta_field &f, int bits)
{
	const u32 full = (1u << bits) - 1;
	switch (r() % 4) {
	case 0: return r() & full;
	case 1: return (f.lo + (r() % 5) - 2) & full;       // around the bounds, where the cascade decides
	case 2: return (f.hi + (r() % 5) - 2) & full;
	default: return (f.lo ^ (1u << (r() % bits))) & full;
	}
}

int main()
{
	std::mt19937 r(8550);
	for (int round = 0; round < 3000; round++) {
		tta board;
		u8 ram[0x10][256] = {};
		auto rd = [&](int p, int row) { return ram[p - 0x70][row]; };
		auto wr = [&](int p, int row, u8 v) { ram[p - 0x70][row] = v; board.ram_write(u16((0xe8 + p - 0x68) << 8 | row), v); };
		tta_eve e[4];
		for (int k = 0; k < 4; k++) {
			e[k].top_mask = 0xff;
			e[k].data_wide = (round & 1) != 0;
			e[k].addr = random_field(r, 24);
			e[k].data = random_field(r, e[k].data_wide ? 16 : 8);
			e[k].pm = u8(r() & r()); e[k].pv = u8(r() & e[k].pm);
			e[k].xm = u8(r() & r()); e[k].xv = u8(r() & e[k].xm);
			e[k].ym = u8(r() & r()); e[k].yv = u8(r() & e[k].ym);
			tta_eve_write(k, e[k], rd, wr);
		}
		for (int t = 0; t < 400; t++) {
			const int k0 = int(r() % 4);
			const u32 a = near(r, e[k0].addr, 24);
			const u16 d = u16(near(r, e[k0].data, e[k0].data_wide ? 16 : 8));
			const u8 p = u8(r()), x = u8(r()), y = u8(r());
			const u8 got = board.comparators(a, d, p, x, y);
			for (int k = 0; k < 4; k++) {
				const bool want = field_match(e[k].addr, a) && field_match(e[k].data, e[k].data_wide ? d : d & 0xff) &&
					(p & e[k].pm) == e[k].pv && (x & e[k].xm) == e[k].xv && (y & e[k].ym) == e[k].yv;
				if (((got >> k) & 1) != want && failures++ < 10)
					printf("event %d: a=%06x d=%04x -> %d, want %d (addr on%d neg%d mask%d %06x-%06x care %06x)\n", k + 1, unsigned(a), d, (got >> k) & 1, want,
						e[k].addr.on, e[k].addr.negate, e[k].addr.masked, unsigned(e[k].addr.lo), unsigned(e[k].addr.hi), unsigned(e[k].addr.care));
			}
		}
	}
	if (failures) { printf("tta_eve: %d mismatches\n", failures); return 1; }
	printf("tta_eve: ok\n");
	return 0;
}
