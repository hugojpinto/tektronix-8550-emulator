// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Trigger Trace Analyzer -- see tta.h, notes/tta.md and notes/tta-os40.md.
#include "machine/tta.h"
#include <cstring>

void tta::sync(u64 now)
{
	if (now <= m_last) return;
	u64 d = now - m_last; m_last = now;
	if (!(m_reg[0] & 0x04)) { m_half = 0; return; }   // test mode only: run mode is timed by the target (target_cycle)
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

// OUT1-5 (bit 0 = OUT1): outputs left high-impedance read high (pull-ups; module 30)
u8 tta::outputs() const
{
	u8 v = u8((const_cast<am9513_device &>(stc).read8(1) >> 1) & 0x1f);
	for (int c = 0; c < 5; c++) if ((stc.counter_mode(c) & 7) == 4) v |= u8(1 << c);
	return v;
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
	case 7: v = u8(0xc0 | (outputs() << 1) | (stc.read8(1) & 0x01)); break;   // Am9513 status, OUT1-5 as the pins are (module 30 expects FF, C1)
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
		// bit 0 off clears the event latches; they hold while it stays on, even with the
		// clock (bit 2) stopped (modules 45 and 46). In run mode OS/40 clears it at the end of
		// every run (7480) and sets it at the start of the next (7325)
		if (!(d & 0x01)) { set_events(0); for (int k = 1; k < 5; k++) gate_w(k, 0); m_armed = 0; m_tr_level = 0; m_tbreak = 0; m_prev_stored = true; }
		m_reg[0] = d;
		break;
	case 2: m_reg[2] = d; m_req = false; break;      // new enables clear the request (module 50)
	case 3: m_trig = u8(d & 0x0f); break;
	case 6: stc.write8(0, d); break;
	case 7: stc.write8(1, d); break;
	default: m_reg[r] = d; break;
	}
}

// The comparators: four events, each the AND of an address, a data, a probe and a
// control comparison. Every comparator page is two 4-bit RAMs, each with its own address
// input (OS/40's EVE computes the two nibbles of one row from different input bytes;
// notes/tta-os40.md):
//   page 76+k  bits 7-4 by A23-16 (M7 M6 M5), bits 3-0 by A15-8 (m3 m2 m1 m0)
//   page 74    by A7-0: event k's pair p1 p0 in bits 2k+1, 2k
//   page 72    by D15-8: 3-bit codes n2 n1 n0, event 1 in bits 2-0, 2 in 5-3, 3's n1 n0 in 7-6
//   page 73    bits 7-4 by the probe (bit 4+k: event k's probe match); bits 3-0 by D15-8
//              (bit 0: event 3's n2; bits 3-1: event 4's n2 n1 n0)
//   page 71    by D7-0: event k's pair q1 q0 in bits 2k+1, 2k
//   page 70    bits 7-4 by the X byte (bus extension 7-5, qualifier 4, OUT4-OUT1 3-0),
//              bits 3-0 by the Y byte (the emulator's bus status); bit 4+k / k for event k
// The codes form a cascade magnitude comparator (inferred from OS/40's encodings, and
// checked against all of them by tests/tta_eve.cpp):
//   ADDR = M7 & !(M6 & (m3 | m2 & p1)) & !(M5 & (m1 | m0 & p0))
//   DATA = !n2 & !(n1 & q1) & !(n0 & q0)
// In test mode every RAM, page 70 included, is addressed by the 2650's A7-0 (TTA.TST passes
// with the function above throughout), and the 2650's writes to them are only stimuli (ram_write).
u8 tta::comparators(u32 a, u16 d, u8 pr, u8 x, u8 y) const
{
	const int a2 = (a >> 16) & 0xff, a1 = (a >> 8) & 0xff, a0 = a & 0xff, d1 = d >> 8, d0 = d & 0xff;
	const u8 p72 = page(0x72, d1), p73lo = page(0x73, d1) & 0x0f, p73hi = page(0x73, pr) >> 4;
	const u8 p74 = page(0x74, a0), p71 = page(0x71, d0);
	const u8 xb = page(0x70, x) >> 4, yb = page(0x70, y) & 0x0f;
	const u8 n[4] = { u8(p72 & 7), u8((p72 >> 3) & 7), u8(((p72 >> 6) & 3) | ((p73lo & 1) << 2)), u8((p73lo >> 1) & 7) };
	u8 ev = 0;
	for (int k = 0; k < 4; k++) {
		const u8 hi = page(0x76 + k, a2) >> 4, lo = page(0x76 + k, a1) & 0x0f;
		const bool M7 = hi & 8, M6 = hi & 4, M5 = hi & 2, m3 = lo & 8, m2 = lo & 4, m1 = lo & 2, m0 = lo & 1;
		const bool p1 = (p74 >> (2 * k + 1)) & 1, p0 = (p74 >> (2 * k)) & 1;
		const bool addr = M7 && !(M6 && (m3 || (m2 && p1))) && !(M5 && (m1 || (m0 && p0)));
		const bool n2 = n[k] & 4, n1 = n[k] & 2, n0 = n[k] & 1;
		const bool q1 = (p71 >> (2 * k + 1)) & 1, q0 = (p71 >> (2 * k)) & 1;
		const bool data = !n2 && !(n1 && q1) && !(n0 && q0);
		if (addr && data && ((p73hi >> k) & 1) && ((xb >> k) & 1) && ((yb >> k) & 1)) ev |= u8(1 << k);
	}
	return ev;
}

// trigger k = event k AND counter k's output; with port 71 bit 4+k set (cons) it also needs
// trigger k-1 (4 before 1) on the preceding cycle of the kind port 71 bits 3-2 select
// (00 any cycle, 01 fetches; OS/40 CONS; TTA.TST module 31)
u8 tta::triggers(u8 ev, u8 outs, bool qualifying)
{
	u8 tr = 0;
	for (int k = 0; k < 4; k++) {
		if (!((ev >> k) & 1) || !((outs >> k) & 1)) continue;
		if (((m_reg[1] >> (4 + k)) & 1) && !((m_armed >> k) & 1)) continue;
		tr |= u8(1 << k);
	}
	if (qualifying) m_armed = u8(((tr << 1) | (tr >> 3)) & 0x0f);   // trigger k arms trigger k+1
	return tr;
}

void tta::store(const u8 rec[8], double t)
{
	for (int p = 0; p < 8; p++) m_ram[(p << 8) | m_acq_ptr] = rec[p];
	if (trace) fprintf(stderr, "[%.7f TTA acquire %02x%02x%02x %02x%02x bus %02x at %02x]\n", t, rec[7], rec[6], rec[5], rec[4], rec[3], rec[1], m_acq_ptr);
	m_acq_ptr++;
}

// Test mode, and the probe test (port 70 bits 1-0 = 11): each 2650 access to a comparator
// page (70-79) looks up a row in every comparator RAM and latches the four event outputs
// until the next one. TTA.TST establishes (notes/tta.md):
// - with port 70 bit 2 set (test mode, 07) every RAM is addressed by the 2650's A7-0; with
//   it clear (03, module 70) the RAMs see the run-mode inputs, of which only the probe is
//   driven (page 73): the target's lines, undriven, read high (inferred: module 70 writes
//   page 73 row 00 between its probe tests, which must not disturb them);
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
	const u8 pr = probe ? probe() : 0x00;
	const u8 outs = outputs();
	const u8 ev = test ? comparators(u32(row * 0x10101), u16(row * 0x101), u8(row), u8(row), u8(row))
	                   : comparators(0xffffff, 0xffff, pr, u8(0xf0 | (outs & 0x0f)), 0xff);
	if (trace) fprintf(stderr, "[%.7f TTA access %04x]\n", double(m_last) / 2e6, a);
	set_events(ev);
	// acquisition: with port 71 bit 1 every access, otherwise those on which event 4 fires,
	// is stored at the pointer, which then advances (port 75). In test mode all eight bytes
	// are the 2650's A7-0 (module 60); otherwise page 6A holds the probe byte (module 70's
	// error report) and the other inputs are idle (0)
	if ((m_reg[1] & 0x02) || (ev & 0x08)) {
		u8 rec[8];
		for (int p = 0; p < 8; p++) rec[p] = test ? u8(row) : p == 2 ? pr : 0;
		store(rec, double(m_last) / 2e6);
	}
	if (!ev) return;
	u8 tr = 0;
	for (int k = 0; k < 4; k++) {
		if (!((ev >> k) & 1) || !((outs >> k) & 1)) continue;
		if (((m_reg[1] >> (4 + k)) & 1) && !((m_armed >> k) & 1)) continue;
		tr |= u8(1 << k);
	}
	m_armed |= u8(((tr << 1) | (tr >> 3)) & 0x0f);
	m_trig |= tr;
	// a new trigger enabled in port 72 interrupts (INT 29, shared with the RTPA). Latched
	// triggers do not when the enables are set later, nor do bits the 2650 writes to port 73
	// (modules 10 and 50)
	if (tr & m_reg[2] & 0x0f) { m_irq = true; m_req = true; }
}

// Run mode: one bus cycle of the program under test. What reaches the board (OS/40's
// encodings, the 8086 diagnostic's module 70; notes/tta-os40.md):
// - the comparators see A23-0, D15-0, the probe, X = bus extension | qualifier | OUT4-1
//   and Y = the bus status byte (comparators above), with the counter outputs as they were
//   before this cycle's counts;
// - the Am9513's inputs: SRC k = event k with the cycle strobe (one count per cycle on
//   which the event is true: "3 events and 1 trigger" for three consecutive matches, TNIX's
//   TTA help); GATE k+1 = trigger k, as a level; SRC5 = port 70 bits 5-4: 00 the bus
//   cycles, 01/11 the emulator clock, 10 the qualifier; GATE1 = the acquisition strobe;
//   F1-F5 from X1 = 5 MHz, which runs with the program;
// - a trigger counts once per occurrence (it stays true while its condition holds) and
//   latches into port 73; a new one with its port 72 bit (3-0) set breaks the program. The
//   break goes to the emulator processor: the Emulator Controller holds its slave and
//   interrupts the 2650 (INT 29, whose OS/40 handler reads 72 & 73), the 8086 board takes it
//   as a break of its own (REASON2 08); the board does not interrupt the 2650 itself here
//   (the 8086 diagnostic's module 70 sets 72 = 91 and takes INT 29 as unexpected);
// - acquisition: every cycle with port 71 bit 1 (acq all), else those on which trigger 4
//   fires (acq ev4), while OUT5 is high (counter 5 ends it: acq ... for n). The record is
//   page 68 bus extension | qualifier (bit 4, inferred) | port 72 bit 4 (bit 0: the run
//   marker OS/40 flips at each GO), 69 bus status, 6A probe, 6B/6C data, 6D-6F address.
void tta::target_cycle(u64 cyc, double hz, const sample &s)
{
	// time, from the emulator processor's clock: X1 for F1-F5, and the emulator clock / 2
	// for SRC5 = EMUCLK (inferred: users manual 3-8)
	if (hz != m_thz || cyc < m_tcyc) { m_thz = hz; m_tcyc = cyc; }
	const double dc = double(cyc - m_tcyc); m_tcyc = cyc;
	m_tx1 += dc * 5e6 / hz;
	const u32 x1 = u32(m_tx1); m_tx1 -= x1;
	if (x1) stc.tick(x1);
	m_temu += dc / 2;
	const int emu = int(m_temu); m_temu -= emu;

	const u8 outs = outputs();
	const u8 pr = probe ? probe() : 0x00;
	const u8 qual = 0x10;                     // the event qualifier, undriven: reads 1 (8086 diag module 70)
	const u8 ev = comparators(s.addr, s.data, pr, u8((s.ext & 0xe0) | qual | (outs & 0x0f)), s.bus);
	const bool qualifying = ((m_reg[1] >> 2) & 3) == 1 ? s.fetch : true;
	const u8 tr = triggers(ev, outs, qualifying);
	const u8 rise = u8(tr & ~m_tr_level);
	m_tr_level = tr;
	m_trig |= tr;
	m_tbreak |= u8(rise & m_reg[2] & 0x0f);
	if (trace && ev) fprintf(stderr, "[%.7f TTA %06x %04x bus %02x: events %x triggers %x]\n", clock ? *clock : 0.0, unsigned(s.addr), s.data, s.bus, ev, tr);

	const u8 rec[8] = { u8((s.ext & 0xe0) | qual | ((m_reg[2] >> 4) & 1)), s.bus, pr, u8(s.data), u8(s.data >> 8), u8(s.addr), u8(s.addr >> 8), u8(s.addr >> 16) };
	int stored = 0;
	if (outs & 0x10) {
		const bool all = m_reg[1] & 0x02;
		if (all || (tr & 0x08)) {
			// acq ev4 after a sequence 3->4 also keeps the cycle that armed trigger 4 (inferred from
			// the users manual's Display 1-14, which lists each ADD M fetch before its operand)
			const double t = clock ? *clock : 0.0;
			if (!all && (m_reg[1] & 0x80) && !m_prev_stored) { store(m_prev_rec, t); stored++; }
			store(rec, t); stored++;
		}
	}
	if (qualifying) { memcpy(m_prev_rec, rec, sizeof rec); m_prev_stored = stored > 0; }

	for (int k = 0; k < 4; k++) gate_w(k + 1, (tr >> k) & 1);
	for (int k = 0; k < 4; k++) if ((ev >> k) & 1) pulse_src(k);
	switch ((m_reg[0] >> 4) & 3) {
	case 0: pulse_src(4); break;
	case 2: break;
	default: for (int i = 0; i < emu; i++) pulse_src(4); break;
	}
	for (int i = 0; i < stored; i++) { gate_w(0, 1); gate_w(0, 0); }
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
	// in test mode (comparators on, test bit set) a 2650 access to a comparator page is only a
	// stimulus: TTA.TST module 31 writes 00, 01, 02... into page 79 row 0 between accesses and
	// expects event 4 to stay, module 60 rewrites page 70 row 40 likewise. Outside test mode the
	// RAMs take writes with the comparators on (the 8086/80186 diagnostics' module 70 at 70 = 03)
	if ((a >> 8) >= 0xf0 && (m_reg[0] & 0x05) == 0x05) return;
	m_ram[a - 0xe800] = d;
}
