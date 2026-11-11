// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA comparator RAM codes -- see tta_eve.h.
#include "system/tta_eve.h"

namespace {
// ---- comparator RAM codes (EVE; notes/tta-os40.md "The codes OS/40 writes"): what event k's
// bits hold in the row of input byte b, for a comparison of one byte of a multi-byte value
// L..H (or a value under a mask). The board combines them (tta::compare):
//   ADDR = M7 & !(M6 & (m3 | m2 & p1)) & !(M5 & (m1 | m0 & p0)),
//   DATA = !n2 & !(n1 & q1) & !(n0 & q0)
enum kind_t { K_CLR, K_RANGE, K_NOT, K_MASK };
struct cmp { kind_t k = K_CLR; u32 lo = 0, hi = 0, care = 0; };
cmp to_cmp(const tta_field &f)
{
	cmp c;
	if (!f.on) return c;
	if (f.masked) { c.k = K_MASK; c.lo = f.lo & f.care; c.care = f.care; return c; }
	c.k = f.negate ? K_NOT : K_RANGE; c.lo = f.lo; c.hi = f.hi;
	return c;
}
int byte_of(u32 v, int i) { return int((v >> (8 * i)) & 0xff); }
bool match_byte(const cmp &c, int i, int b) { return ((b ^ byte_of(c.lo, i)) & byte_of(c.care, i)) == 0; }
// A23-16: M7 M6 M5 (the high nibble of page 76+k; bit 4 unused)
u8 code_top(const cmp &c, int b)
{
	const int L = byte_of(c.lo, 2), H = byte_of(c.hi, 2);
	switch (c.k) {
	case K_CLR: return 0xe;
	case K_MASK: return match_byte(c, 2, b) ? 0xe : 0x0;
	case K_RANGE: return u8(((L <= b && b <= H) << 3) | ((b == H) << 2) | ((b == L) << 1));
	default: return u8((!(L < b && b < H) << 3) | ((b == H) << 2) | ((b == L) << 1));
	}
}
// A15-8: m3 m2 m1 m0 (the low nibble of page 76+k)
u8 code_mid(const cmp &c, int b)
{
	const int L = byte_of(c.lo, 1), H = byte_of(c.hi, 1);
	switch (c.k) {
	case K_CLR: return 0x5;
	case K_MASK: return match_byte(c, 1, b) ? 0x5 : 0xa;
	case K_RANGE: return u8(((b > H) << 3) | ((b == H) << 2) | ((b < L) << 1) | (b == L));
	default:
		if (byte_of(c.lo, 2) != byte_of(c.hi, 2)) return u8(((b < H) << 3) | ((b == H) << 2) | ((b > L) << 1) | (b == L));
		{ const int in = L < b && b < H; return u8((in << 3) | ((b == H) << 2) | (in << 1) | (b == L)); }
	}
}
// the last byte's pair (p1 p0 for A7-0, q1 q0 for D7-0); `upper_equal`: all upper bytes of
// the bounds are equal (an=/dn= then mark the excluded range itself)
u8 code_low(const cmp &c, int b, bool upper_equal)
{
	const int L = byte_of(c.lo, 0), H = byte_of(c.hi, 0);
	switch (c.k) {
	case K_CLR: return 0;
	case K_MASK: return match_byte(c, 0, b) ? 0 : 3;
	case K_RANGE: return u8(((b > H) << 1) | (b < L));
	default:
		if (upper_equal) return (L <= b && b <= H) ? 3 : 0;
		return u8(((b <= H) << 1) | (b >= L));
	}
}
// D15-8: n2 n1 n0. The 8-bit emulators compare D15-8 masked with value and mask 0 (011)
u8 code_data_hi(const cmp &c, int b, bool wide)
{
	if (!wide || c.k == K_CLR) return 3;
	const int L = byte_of(c.lo, 1), H = byte_of(c.hi, 1);
	switch (c.k) {
	case K_MASK: return match_byte(c, 1, b) ? 3 : 4;
	case K_RANGE: return u8(((b < L || b > H) << 2) | ((b == H) << 1) | (b == L));
	default: return u8(((L < b && b < H) << 2) | ((b == H) << 1) | (b == L));
	}
}
}

void tta_eve_write(int k, const tta_eve &e, const std::function<u8(int, int)> &rd, const std::function<void(int, int, u8)> &wr)
{
	const cmp a = to_cmp(e.addr), d = to_cmp(e.data);
	const bool a_upper_equal = byte_of(a.lo, 2) == byte_of(a.hi, 2) && byte_of(a.lo, 1) == byte_of(a.hi, 1);
	const bool d_upper_equal = !e.data_wide || byte_of(d.lo, 1) == byte_of(d.hi, 1);
	for (int b = 0; b < 256; b++) {
		// page 76+k: M7 M6 M5 by A23-16 (the row masked as EVE does), m3-m0 by A15-8
		wr(0x76 + k, b, u8((code_top(a, b & e.top_mask) << 4) | code_mid(a, b)));
		wr(0x74, b, u8((rd(0x74, b) & ~(3 << (2 * k))) | (code_low(a, b, a_upper_equal) << (2 * k))));
		wr(0x71, b, u8((rd(0x71, b) & ~(3 << (2 * k))) | (code_low(d, b, d_upper_equal) << (2 * k))));
		// the D15-8 code n2 n1 n0: event 1 in page 72 bits 2-0, 2 in bits 5-3, 3 in bits 7-6 and
		// page 73 bit 0, 4 in page 73 bits 3-1
		const u8 n = code_data_hi(d, b, e.data_wide);
		u8 p72 = rd(0x72, b), p73 = rd(0x73, b);
		switch (k) {
		case 0: p72 = u8((p72 & ~0x07) | n); break;
		case 1: p72 = u8((p72 & ~0x38) | (n << 3)); break;
		case 2: p72 = u8((p72 & ~0xc0) | ((n & 3) << 6)); p73 = u8((p73 & ~0x01) | (n >> 2)); break;
		default: p73 = u8((p73 & ~0x0e) | (n << 1)); break;
		}
		// the probe: page 73 bit 4+k by P7-0; X and Y: page 70 bits 4+k and k
		p73 = u8((p73 & ~(0x10 << k)) | (((b & e.pm) == e.pv ? 1 : 0) << (4 + k)));
		wr(0x72, b, p72); wr(0x73, b, p73);
		wr(0x70, b, u8((rd(0x70, b) & ~(0x11 << k)) | (((b & e.xm) == e.xv ? 1 : 0) << (4 + k)) | (((b & e.ym) == e.yv ? 1 : 0) << k)));
	}
}
