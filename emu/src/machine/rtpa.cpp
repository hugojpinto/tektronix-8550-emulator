// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Real-Time Prototype Analyzer -- see rtpa.h and notes/rtpa.md.
#include "machine/rtpa.h"

// ---------------------------------------------------------------- 8253

void rtpa::counter::write(u8 d)
{
	switch (rl) {
	case 1: cr = u16((cr & 0xff00) | d); pending = true; break;
	case 2: cr = u16((cr & 0x00ff) | (d << 8)); pending = true; break;
	default:
		if (!wr_msb) cr = u16((cr & 0xff00) | d);
		else { cr = u16((cr & 0x00ff) | (d << 8)); pending = true; }
		wr_msb = !wr_msb;
	}
}

u8 rtpa::counter::read()
{
	if (rl == 1) return u8(ce);
	if (rl == 2) return u8(ce >> 8);
	u8 v = rd_msb ? u8(ce >> 8) : u8(ce);
	rd_msb = !rd_msb;
	return v;
}

// The count loads on the first clock after it is written; later clocks count down.
// Mode 0: OUT rises at zero. Mode 2 (rate generator): OUT is low while the count
// is 1, and rises when it reloads. Other modes just count.
bool rtpa::counter::clock()
{
	if (pending) { ce = cr; pending = false; out = mode == 2; return false; }
	switch (mode) {
	case 0:
		if (ce == 0 || out) return false;
		if (--ce == 0) { out = true; return true; }
		return false;
	case 2:
		if (--ce == 1) { out = false; return false; }
		if (ce == 0) { ce = cr; out = true; return true; }
		return false;
	default:
		ce--;
		return false;
	}
}

void rtpa::pit::control(u8 d)
{
	int sc = d >> 6;
	if (sc == 3) return;
	counter &k = c[sc];
	if (((d >> 4) & 3) == 0) return;          // counter latch: not used
	k.rl = (d >> 4) & 3; k.mode = (d >> 1) & 7; if (k.mode > 5) k.mode &= 3;
	k.wr_msb = k.rd_msb = false; k.out = k.mode != 0;
}

// ---------------------------------------------------------------- ports

u8 rtpa::read(int r)
{
	u8 v = 0xff;
	switch (r) {
	case 2: v = u8(0x10 | (m_status & 0x0f)); break;           // bit 4: board present
	case 3: v = (m_index & 6) == 6 ? 0xff : m_pit[m_index & 1].c[(m_index >> 1) & 3].read(); break;
	case 4: v = u8(~m_pc_last); break;                          // latches read inverted
	case 5: v = u8(~(m_pc_last >> 8)); break;
	case 6: v = u8(~m_pc_next); break;
	case 7: v = u8(~(m_pc_next >> 8)); break;
	case 1: v = trace_read(); break;
	default: v = 0xff; break;
	}
	log("rd", r, v);
	return v;
}

void rtpa::write(int r, u8 d)
{
	log("wr", r, d);
	switch (r) {
	case 0:                                   // strobe: clocks channel 1's pass and G.P. counters
		m_c0 = d;
		m_pit[0].c[0].clock(); m_pit[0].c[1].clock();
		break;
	case 1: m_c1 = d; break;
	case 2:                                   // event enables; writing it re-arms
		m_c2 = d;
		m_status = 0; m_trig = false; m_stopped = false; m_armed = false; m_delay[0] = m_delay[1] = false;
		m_tr_rd = 0; m_tr_byte = 0; m_frozen = false;
		break;
	case 3:
		// bit 4 (channel 2 pass count enable) also strobes channel 2's pass and G.P.
		// counters on its rising edge, as C0 does for channel 1 (counter test; PASS2 arm)
		if (!(m_c3 & 0x10) && (d & 0x10)) { m_pit[1].c[0].clock(); m_pit[1].c[1].clock(); }
		m_c3 = d;
		break;
	case 4: m_ram[m_index & 15] = d; break;   // comparator field, index = 2*field + event
	case 5:                                   // 8253 register: index = 2*A + event
		if ((m_index & 6) == 6) m_pit[m_index & 1].control(d);
		else m_pit[m_index & 1].c[(m_index >> 1) & 3].write(d);
		break;
	case 6: m_index = d; break;
	case 7: m_mode[m_index & 3] = d; m_index = u8((m_index & 0xfc) | ((m_index + 1) & 3)); break;   // [0/1] address, [2/3] data compare mode; auto-increments (DOS/50 writes all four after C6 = 0)
	}
}

// ---------------------------------------------------------------- comparators

// Fields (C4 RAM, per event e at 2*k + e), inverted ones marked ~:
//   k=0 ~address low, k=1 ~data, k=2 ~address high, k=3 (unused here, written FF),
//   k=4 bus value, k=5 ~probe pattern (T), k=6 bus mask, k=7 probe mask (TM).
// Bus bits: 01 memory (MNIO), 02 write (WNR), 04 fetch (F), F0 control lines (inactive high).
// Address mode (C7 index e): 02 low byte equal, 04 equal, 08 <=, 01 >=, 0 don't care.
// Data mode (C7 index 2+e): 02 equal, 0 don't care.
bool rtpa::match(int e, u16 a, u8 d, u8 cyc, u8 probe) const
{
	u16 ea = u16(u8(~m_ram[0 + e]) | (u8(~m_ram[4 + e]) << 8));
	u8 ed = u8(~m_ram[2 + e]);
	u8 bv = m_ram[8 + e], bm = m_ram[12 + e];
	if ((cyc & bm) != (bv & bm)) return false;
	switch (m_mode[e]) {
	case 0x02: if (u8(a) != u8(ea)) return false; break;
	case 0x04: if (a != ea) return false; break;
	case 0x08: if (a > ea) return false; break;
	case 0x01: if (a < ea) return false; break;
	default: break;
	}
	if (m_mode[2 + e] == 0x02 && d != ed) return false;
	if ((probe ^ u8(~m_ram[10 + e])) & m_ram[14 + e]) return false;   // probe T (inverted) under mask TM
	return true;
}

void rtpa::instr_begin(u16 pc) { m_cur_pc = pc; }

// An event occurred on channel e: count it (pass), start its delay, then act:
// arm (channel 1 under ARM/FRZ), or trigger. Under LIM the combined match is a
// channel-1 occurrence and triggers both events.
void rtpa::occurrence(int e, bool &hit)
{
	bool pass = e == 0 ? (m_c2 & 0x10) : (m_c3 & 0x10);
	bool delay = e == 0 ? (m_c2 & 0x20) : (m_c3 & 0x20);
	if (gp_source() == e) clock_gp(gp_gate());             // G.P. counts this channel's events
	if (pass && !m_pit[e].c[0].clock()) return;
	if (delay) { m_delay[e] = true; return; }
	qualified(e);
	hit = true;
}

// A channel's qualified event (after pass count and delay)
void rtpa::qualified(int e)
{
	if (e == 0 && (m_c2 & 0x04)) {                         // ARM: event 1 arms event 2
		m_armed = true;
		if (m_c1 & 0x20) m_frozen = true;                    // FRZ: the trace freezes at the arming event
		return;
	}
	if (m_c2 & 0x02) { trigger(0); trigger(1); return; }    // LIM
	trigger(e);
}

void rtpa::bus(u16 a, u8 d, bool fetch, bool write, bool io)
{
	if (m_stopped || !(m_c3 & 0x08)) return;   // C3 bit 3: analyser running (DOS/50 clears it after a break: the trace freezes)
	u8 cyc = u8(0xf0 | (io ? 0 : 0x01) | (write ? 0x02 : 0) | (fetch ? 0x04 : 0));
	// probe inputs: open (00), or looped back to A0-A7(L) -- the low address byte, inverted
	const u8 probe = probe_loopback ? u8(~a) : 0x00;
	if (gp_source() == 4) clock_gp(gp_gate());                 // every bus cycle
	if (!m_frozen && traced(cyc)) {
		if (gp_source() == 2) clock_gp(gp_gate());             // every stored trace entry
		// bus byte: cycle bits, plus the upper bits of event 1's bus-value register as a
		// run tag (DOS/50 steps it by 08 at every GO; DRT shows the entries of the last run)
		m_trace[m_tr_head] = { u8(a >> 8), u8(a), u8((cyc & 0x07) | (m_ram[8] & 0xf8)), 0, d, probe };
		m_tr_head = (m_tr_head + 1) % TRACE;
		if (m_tr_count < TRACE) m_tr_count++;
	}
	if (fetch) {
		// counting first: a counter gated by this cycle's own event starts with the next fetch
		if (gp_source() == 3) clock_gp(gp_gate());
		for (int e = 0; e < 2; e++)
			if (m_delay[e] && m_pit[e].c[2].clock()) { m_delay[e] = false; qualified(e); }
	}
	bool en1 = (m_c2 & 0x07) != 0, en2 = (m_c2 & 0x0a) != 0;   // LIM (02) uses both events, ARM (04) event 1
	bool m1 = en1 && match(0, a, d, cyc, probe), m2 = en2 && match(1, a, d, cyc, probe);
	if (trace && (m1 || m2)) fprintf(stderr, "[%.4f RTPA match%s%s at %04x d=%02x cyc=%02x pc=%04x]\n", clock ? *clock : 0.0, m1 ? " E1" : "", m2 ? " E2" : "", a, d, cyc, m_cur_pc);
	bool hit = false;
	if (m_c2 & 0x02) {                        // LIM: both events in the same cycle (channel 1's pass/delay apply)
		if (m1 && m2) occurrence(0, hit);
		return;
	}
	if (m1) occurrence(0, hit);
	if (m2 && (!(m_c2 & 0x04) || m_armed)) occurrence(1, hit);
}

void rtpa::instr_end(u16 next_pc, int tstates, double hz)
{
	// time-based G.P. sources: emulator clock, and a 1 MHz time base -- usec directly,
	// msec through channel 2's counter 1 (DOS/50 loads it with 1000)
	if (!m_stopped && (m_c3 & 0x08)) {
		int src = gp_source();
		if (src == 5) for (int i = 0; i < tstates; i++) clock_gp(gp_gate());
		if (src >= 6) {
			m_us_frac += tstates * 1e6 / hz;
			for (; m_us_frac >= 1.0; m_us_frac -= 1.0) {
				if (src == 7) { clock_gp(gp_gate()); continue; }
				u16 n = m_pit[1].c[1].cr ? m_pit[1].c[1].cr : 1000;   // prescaler: one tick per n us
				if (++m_prescale >= n) { m_prescale = 0; clock_gp(gp_gate()); }
			}
		}
	}
	if (!m_trig || m_stopped) return;
	m_pc_last = m_cur_pc; m_pc_next = next_pc;
	m_status |= 0x08;
	m_stopped = true;
	m_trig = false;                           // one break per trigger: once the Emulator Controller releases the
	                                          // hold (F9), the status keeps it, but nothing breaks again until a
	                                          // C2 write re-arms (ASV: RTPA.TST's last trigger broke the Z80 test, 07/0000)
	m_tr_rd = 0; m_tr_byte = 0;               // the trace reads out oldest first
	m_irq = true;
	if (trace) fprintf(stderr, "[%.4f RTPA break: status %02x PC-LAST %04x PC-NEXT %04x, %d traced (C1 %02x)]\n", clock ? *clock : 0.0, m_status, m_pc_last, m_pc_next, m_tr_count, m_c1);
}

// ---------------------------------------------------------------- trace buffer

// RTT qualifier (C1, values from DOS/50's RTT): 01 F fetch, 02 I I/O, 04 R read,
// 08 W write, 10 M memory; 00 = ALL. IR = 06, IW = 0A, MR = 14, MW = 18.
bool rtpa::traced(u8 cyc) const
{
	u8 q = m_c1 & 0x1f;
	if (q == 0) return true;
	bool fetch = cyc & 0x04, mem = cyc & 0x01, write = cyc & 0x02;
	if ((q & 0x01) && !fetch) return false;
	if (!(q & 0x01) && fetch) return false;
	if ((q & 0x12) && !(((q & 0x10) && mem) || ((q & 0x02) && !mem))) return false;
	if ((q & 0x0c) && !(((q & 0x08) && write) || ((q & 0x04) && !write))) return false;
	return true;
}

// Read out through C1, six bytes per entry, oldest slot first (RTPA.TST 268A):
// address low, address high, bus status (not inverted), data, extension, probe;
// all but the bus status read inverted.
u8 rtpa::trace_read()
{
	// a 128-slot circular RAM read from the write pointer: the last entry read is the newest
	const entry &e = m_trace[(m_tr_head + m_tr_rd) % TRACE];
	u8 v;
	switch (m_tr_byte) {
	case 0: v = u8(~e.lo); break;
	case 1: v = u8(~e.hi); break;
	case 2: v = e.bus; break;
	case 3: v = u8(~e.data); break;
	case 4: v = u8(~e.ext); break;
	default: v = u8(~e.probe); break;
	}
	if (++m_tr_byte == 6) { m_tr_byte = 0; m_tr_rd = (m_tr_rd + 1) % TRACE; }
	return v;
}
