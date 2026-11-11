// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Trigger Trace Analyzer -- see tta.h and notes/tta.md.
#include "machine/tta.h"

void tta::sync(u64 now)
{
	if (now <= m_last) return;
	u64 d = now - m_last; m_last = now;
	if (!(m_reg[0] & 0x04)) { m_half = 0; return; }   // port 70 bit 2 off: no counter clock
	// 5 MHz from 2 MHz 2650 clocks: 5 X1 clocks per 2 CPU clocks
	u64 x = d * 5 + m_half;
	m_half = u32(x & 1);
	stc.tick(u32(x >> 1));
}

void tta::src_w(int n, int l)
{
	switch (n) {
	case 0: stc.source1_w(l); break;
	case 1: stc.source2_w(l); break;
	case 2: stc.source3_w(l); break;
	case 3: stc.source4_w(l); break;
	default: stc.source5_w(l); break;
	}
}

void tta::gate_w(int n, int l)
{
	switch (n) {
	case 0: stc.gate1_w(l); break;
	case 1: stc.gate2_w(l); break;
	case 2: stc.gate3_w(l); break;
	case 3: stc.gate4_w(l); break;
	default: stc.gate5_w(l); break;
	}
}

bool tta::ctr_out(int c) const
{
	if ((stc.counter_mode(c) & 7) == 4) return true;    // high impedance: pulled up on the board
	return (const_cast<am9513_device &>(stc).read8(1) >> (c + 1)) & 1;
}

u8 tta::read(int r, u64 now)
{
	sync(now);
	u8 v;
	switch (r) {
	case 0: v = u8(m_reg[0] | 0xc0); break;           // bits 7-6 read high: the board is there (module 00)
	case 1: case 2: case 4: v = m_reg[r]; break;
	case 3: v = u8(m_trig & 0x0f); break;             // trigger latches (module 30), or what was written (module 10)
	case 5: v = m_acq_ptr; break;                     // acquisition pointer (module 60)
	case 6: v = stc.read8(0); break;                  // Am9513 data
	case 7: {                                         // Am9513 status: OUT1-5 as the pins are
		v = u8(stc.read8(1) | 0xc0);                  // bits 7-6 read high (module 30 expects FF, C1)
		for (int c = 0; c < 5; c++) if ((stc.counter_mode(c) & 7) == 4) v |= u8(2 << c);
		break;
	}
	default: v = 0xff; break;
	}
	log("rd", r, v);
	return v;
}

void tta::write(int r, u8 d, u64 now)
{
	sync(now);
	log("wr", r, d);
	switch (r) {
	case 0:
		if (!(d & 0x04)) m_armed = 0;                 // stopping clears the arming chain (module 31)
		m_reg[0] = d;
		// bit 0 off clears the event latches; they hold while it stays on, even with the
		// clock (bit 2) stopped (modules 45 and 46)
		if (!(d & 0x01)) set_events(0);
		break;
	case 2: m_reg[2] = d; m_req = false; break;      // new enables clear the request (module 50)
	case 3: m_trig = u8(d & 0x0f); break;
	case 6: stc.write8(0, d); break;
	case 7: stc.write8(1, d); break;
	default: m_reg[r] = d; break;
	}
}

// Comparators on (port 70 bits 1-0 = 11): each access to a comparator page (70-79) looks
// up a row in every comparator RAM and latches the four event outputs until the next one.
// TTA.TST establishes (notes/tta.md):
// - with port 70 bit 2 set (test mode, 07) every RAM is addressed by the 2650's A7-0; with
//   it clear (03, module 70) page 73 is addressed by the probe lines;
// - event k fires when page 73 has bit 3+k set and page 74 has event k's bit pair = 00
//   (modules 30, 31, 46, 60, 70); pages 70-72 and 76-79 do not take part in the tests;
// - event k drives Am9513 SRC k and GATE k+1 as a level (modules 45, 46);
// - trigger k = event k AND counter k's output (module 30);
// - with port 71 bit (3+k) set, trigger k also needs trigger k-1 (4 before 1) on an
//   earlier access (module 31); port 73 latches the triggers;
// - acquisition (module 60, 70): see below.
void tta::ram_access(u64 now, u16 a)
{
	sync(now);
	// only the comparator RAMs (pages 70-79) are looked up: an access to the acquisition
	// memory (68-6F) clocks nothing (module 60 reads it back between samples)
	if (!watching() || (a >> 8) < 0xf0) return;
	const bool test = m_reg[0] & 0x04;
	const int row = a & 0xff;
	const u8 pr = probe ? probe() : 0xff;
	u8 p73 = page(0x73, test ? row : pr), p74 = page(0x74, row);
	u8 ev = 0;
	for (int k = 0; k < 4; k++)
		if (((p73 >> (4 + k)) & 1) && !((p74 >> (2 * k)) & 3)) ev |= u8(1 << k);
	set_events(ev);
	// acquisition: with port 71 bit 1 every access, otherwise those on which event 4 fires,
	// is stored at the pointer, which then advances (port 75). In test mode all eight bytes
	// are the 2650's A7-0 (module 60); otherwise page 6A holds the probe byte (module 70's
	// error report) and the other bytes' inputs are not known (stored as 0).
	if ((m_reg[1] & 0x02) || (ev & 0x08)) {
		for (int p = 0; p < 8; p++) m_ram[(p << 8) | m_acq_ptr] = test ? u8(row) : p == 2 ? pr : 0;
		if (trace) fprintf(stderr, "[%.7f TTA acquire %02x at %02x]\n", double(m_last) / 2e6, test ? row : pr, m_acq_ptr);
		m_acq_ptr++;
	}
	if (!ev) return;
	u8 tr = 0;
	for (int k = 0; k < 4; k++) {
		if (!((ev >> k) & 1) || !ctr_out(k)) continue;
		bool needs_arm = (m_reg[1] >> (4 + k)) & 1;
		if (needs_arm && !((m_armed >> k) & 1)) continue;
		tr |= u8(1 << k);
	}
	m_armed |= u8(((tr << 1) | (tr >> 3)) & 0x0f);   // trigger k arms trigger k+1
	m_trig |= tr;
	// a new trigger enabled in port 72 interrupts (INT 29, shared with the RTPA). Latched
	// triggers do not when the enables are set later, nor do bits the 2650 writes to port 73
	// (modules 10 and 50)
	if (tr & m_reg[2] & 0x0f) { m_irq = true; m_req = true; }
}

void tta::set_events(u8 ev)
{
	u8 ch = ev ^ m_ev;
	m_ev = ev;
	if (ch && trace) fprintf(stderr, "[%.7f TTA events %x]\n", double(m_last) / 2e6, ev);
	for (int k = 0; k < 4; k++)
		if ((ch >> k) & 1) { int l = (ev >> k) & 1; src_w(k, l); gate_w(k + 1, l); }
}

u8 tta::ram_read(u16 a)
{
	if ((a >> 8) == 0xf5) return 0xff;                // no RAM in page F5: system RAM is inhibited there too
	return m_ram[a - 0xe800];
}

void tta::ram_write(u16 a, u8 d)
{
	if ((a >> 8) == 0xf5) return;
	m_ram[a - 0xe800] = d;
}
