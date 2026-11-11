// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Intel 8257 DMA controller -- register model. Transfers are driven by the
// owning machine (which knows how DRQ lines and memory are wired).
#ifndef TEK_I8257_H
#define TEK_I8257_H
#include "emu.h"

class i8257
{
public:
	struct channel { u16 addr = 0, count = 0; };   // count: bits 13-0 = N-1, 14 = write, 15 = read
	channel ch[4];
	u8 mode = 0, status = 0;
	bool ff = false;

	u8 read(int reg)
	{
		if (reg & 8) { u8 s = status; status &= ~0x0f; return s; }
		channel &c = ch[(reg >> 1) & 3];
		u16 v = (reg & 1) ? c.count : c.addr;
		u8 r = ff ? u8(v >> 8) : u8(v);
		ff = !ff;
		return r;
	}
	void write(int reg, u8 d)
	{
		if (reg & 8) { mode = d; ff = false; return; }
		channel &c = ch[(reg >> 1) & 3];
		u16 &v = (reg & 1) ? c.count : c.addr;
		v = ff ? u16((v & 0x00ff) | (d << 8)) : u16((v & 0xff00) | d);
		ff = !ff;
		// autoload: writing channel 2 also loads channel 3
		if ((mode & 0x80) && ((reg >> 1) & 3) == 2) ch[3] = ch[2];
	}
	void reset() { mode = 0; status = 0; ff = false; }

	bool enabled(int n) const { return mode & (1 << n); }
	bool is_read(int n) const { return ch[n].count & 0x8000; }    // memory -> I/O
	bool is_write(int n) const { return ch[n].count & 0x4000; }   // I/O -> memory

	// one byte transferred on channel n; returns true at terminal count
	bool step(int n)
	{
		channel &c = ch[n];
		u16 cnt = c.count & 0x3fff;
		bool tc = (cnt == 0);
		c.addr++;
		c.count = u16((c.count & 0xc000) | ((cnt - 1) & 0x3fff));
		if (tc) {
			status |= u8(1 << n);
			if (n == 2 && (mode & 0x80)) { ch[2] = ch[3]; }
			else if (mode & 0x40) mode &= u8(~(1 << n));   // TC stop
		}
		return tc;
	}
};
#endif
