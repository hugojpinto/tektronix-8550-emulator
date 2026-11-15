// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Real-Time Prototype Analyzer -- see rtpa.h and notes/rtpa.md.
#include "machine/rtpa.h"
#include <algorithm>
#include <cmath>

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
// Mode 0: OUT rises at zero. Mode 1 (the delays' one-shot): started by a trigger (the
// load), OUT rises at zero. Mode 2 (rate generator): OUT is low while the count is 1, and
// rises when it reloads. Other modes just count.
bool rtpa::counter::clock()
{
	if (pending) { ce = cr; pending = false; out = mode == 2; return false; }
	switch (mode) {
	case 0: case 1:
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
	// the 8253's counter 1 is the delay (subaddresses 4/5) and counter 2 the G.P. counter or the
	// msec divider (subaddresses 2/3): Millennium's 9508 RTPA equates ("4 DELAY 1 CTR (CTR 1
	// 1ST GROUP)", "2 REAL TIME CTR (CTR 2 1ST GROUP)", "3 DVD BY 1000 (CTR 2 2ND GROUP)") and its
	// mode words (72 "DELAY CNT", B0 "G.P. CNTR", B4 "MSEC CNTR"). c[] is indexed by role
	counter &k = c[sc == 1 ? 2 : sc == 2 ? 1 : 0];
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
		m_tr_rd = 0; m_tr_byte = 0; m_frozen = false; m_thaw = false;
		break;
	case 3:
		// bit 4 (channel 2 pass count enable) also strobes channel 2's pass and G.P.
		// counters on its rising edge, as C0 does for channel 1 (counter test; PASS2 arm)
		if (!(m_c3 & 0x10) && (d & 0x10)) { m_pit[1].c[0].clock(); m_pit[1].c[1].clock(); }
		if (!(m_c3 & 0x08) && (d & 0x08)) m_abs_us = now_us();   // a run starts: the time base is the master's
		m_c3 = d;
		break;
	case 4:                                   // comparator field, index = 2*field + event; auto-increments
		// over the 16 subaddresses ("16 SUB ADDRESSES (WRITE IN ORDER)", Millennium's 9508 RTPA
		// equates; the 8002 diagnostics' hardware analyzer tests write all 16 after C6 = 0)
		m_ram[m_index & 15] = d;
		m_index = u8((m_index & 0xf0) | ((m_index + 1) & 15));
		break;
	case 5:                                   // 8253 register: index = 2*A + event
		if ((m_index & 6) == 6) m_pit[m_index & 1].control(d);
		else {
			counter &k = m_pit[m_index & 1].c[(m_index >> 1) & 3];
			const bool was = k.pending;
			k.write(d);
			// the msec divider (subaddress 3, a mode-2 rate generator clocked by the 1 us time
			// base) restarts from its count when one is written: its first output comes N us
			// after the next 1 us edge, then one every N us
			if ((m_index & 7) == 3 && k.pending && !was) m_msec_load_us = std::ceil(now_us());
		}
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
// Data mode (C7 index 2+e): the same codes on the data byte (02 and 04 equal), 0 don't care.
bool rtpa::ctl_match(int e, u8 cyc) const { return ((cyc ^ m_ram[8 + e]) & m_ram[12 + e]) == 0; }

bool rtpa::addr_match(int e, u16 a) const
{
	u16 ea = u16(u8(~m_ram[0 + e]) | (u8(~m_ram[4 + e]) << 8));
	switch (m_mode[e]) {
	case 0x02: return u8(a) == u8(ea);
	case 0x04: return a == ea;
	case 0x08: return a <= ea;
	case 0x01: return a >= ea;
	default: return true;
	}
}

bool rtpa::match(int e, u16 a, u8 d, u8 cyc, u8 probe) const
{
	u8 ed = u8(~m_ram[2 + e]);
	if (!ctl_match(e, cyc) || !addr_match(e, a)) return false;
	switch (m_mode[2 + e]) {   // the address codes; >= and <= from the 8002 diagnostics' hardware
	case 0x02: case 0x04: if (d != ed) return false; break;   // analyzer test (TRACE FETCHES: D >= C2, D <= C2)
	case 0x08: if (d > ed) return false; break;
	case 0x01: if (d < ed) return false; break;
	default: break;
	}
	if ((probe ^ u8(~m_ram[10 + e])) & m_ram[14 + e]) return false;   // probe T (inverted) under mask TM
	return true;
}

void rtpa::clock_gp(bool gate)
{
	// the delays take the same clock, gated the same way: under ARM an event-1 delay never
	// runs out ("If a delay count (option C) is added to the EVT1 command line ... an EVT1
	// trigger cannot be generated", 070-2785 3.B.4: the program runs on to its halt)
	if (!gate) return;
	m_pit[0].c[1].clock();
	for (int e = 0; e < 2; e++)
		if (m_delay[e] && m_pit[e].c[2].clock()) { m_delay[e] = false; qualified(e); }
}

void rtpa::instr_begin(u16 pc)
{
	// the 1 us time base runs on while the program is stopped (an SVC, the master's work): follow
	// the master across such gaps, not across the small lead it has within a scheduling slice
	if (now_us() > m_abs_us + 20) m_abs_us = now_us();
	m_cur_pc = pc; m_pos = 0; m_open_at = m_stop_at = -1; m_gate_before = gp_gate();
}

// An event occurred on channel e: count it (pass), start its delay, then act:
// arm (channel 1 under ARM/FRZ), or trigger. Under LIM the combined match is a
// channel-1 occurrence.
void rtpa::occurrence(int e, bool &hit)
{
	bool pass = e == 0 ? (m_c2 & 0x10) : (m_c3 & 0x10);
	bool delay = e == 0 ? (m_c2 & 0x20) : (m_c3 & 0x20);
	if (gp_source() == e) clock_gp(gp_gate());             // G.P. counts this channel's events
	if (pass && !m_pit[e].c[0].clock()) return;
	// a delay restarts from its full count at each (passed) occurrence, unless one is already
	// running: 070-2785 4.A.1 (BIF IND C) shows event 1 (P=10 C=20) breaking every 10 passes,
	// and event 2 (P=10 C=150, about 12 passes) at passes 22 and 42, not 32
	if (delay) { if (!m_delay[e]) { m_delay[e] = true; m_pit[e].c[2].pending = true; } return; }
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
	if (e == 1 && (m_c2 & 0x04)) {
		// "the EVT2 trigger breaks the program execution and stops the general purpose counter.
		// In addition, the EVT2 trigger arms the EVT1 comparator and enables the RTT buffer"
		// (070-2785 5.A.4): event 1 must come again, and under FRZ the trace runs again from the
		// next instruction until it does (5.A.5's listing; 5.A.3: one break, the count stops at it)
		m_armed = false;
		if (m_c1 & 0x20) m_thaw = true;
	}
	// LIM breaks as event 1; the status shows event 2 only if its whole comparison matched too
	// (the 8002 diagnostics' TRACE ALL TEST expects 11 when event 2's data differs)
	trigger(e);
}

void rtpa::bus(u16 a, u8 d, bool fetch, bool write, bool io)
{
	if (m_stopped || !(m_c3 & 0x08)) return;   // C3 bit 3: analyser running (DOS/50 clears it after a break: the trace freezes)
	if (!io && io_page >= 0 && (a >> 8) == io_page) io = true;
	u8 cyc = u8(0xf0 | (io ? 0 : 0x01) | (write ? 0x02 : 0) | (fetch ? 0x04 : 0));
	// probe inputs: open (00), or looped back to A0-A7(L) -- the low address byte, inverted
	const u8 probe = probe_loopback ? u8(~a) : 0x00;
	const bool trig_before = m_trig;
	// the status's event bits record which events match on the cycle that breaks, whatever C2
	// enables; an event with no address compare (C7 00) does not count there (the 8002
	// diagnostics' hardware analyzer test: its descriptors' expected status, subtests 1, 3-6)
	const u8 now = u8((m_mode[0] && match(0, a, d, cyc, probe) ? 1 : 0) | (m_mode[1] && match(1, a, d, cyc, probe) ? 2 : 0));
	struct snap { rtpa &r; bool before; u8 now; ~snap() { if (!before && r.m_trig) r.m_status |= now; } } on_break{ *this, trig_before, now };
	// where this cycle ends within the instruction, for the time-based counts (see states_fetch)
	const int len = fetch ? states_fetch : io ? states_io : states_mem;
	struct place { rtpa &r; bool trig0, gate0; int end; ~place() {
		r.m_pos = end;
		if (!gate0 && r.gp_gate() && r.m_open_at < 0) r.m_open_at = end;
		if (!trig0 && r.m_trig && r.m_stop_at < 0) r.m_stop_at = end;
	} } on_end{ *this, trig_before, gp_gate(), m_pos + len };
	// the G.P. counter's bus-driven sources stop at the break: the rest of the breaking
	// instruction is not counted (070-2785 Procedure 2: CNT C reads 739 for a delay of 500 bus
	// cycles after the 239th; the diagnostics' TRACE I/O WRITES ONLY: 8 entries traced, 7 counted)
	const bool counting = !m_trig;
	if (gp_source() == 4 && counting) clock_gp(gp_gate());     // every bus cycle
	if (!m_frozen && traced(cyc)) {
		if (gp_source() == 2 && counting) clock_gp(gp_gate());   // every stored trace entry
		// bus byte: cycle bits, plus the upper bits of event 1's bus-value register as a
		// run tag (DOS/50 steps it by 08 at every GO; DRT shows the entries of the last run)
		m_trace[m_tr_head] = { u8(a >> 8), u8(a), u8((cyc & 0x07) | (m_ram[8] & 0xf8)), 0, d, probe };
		m_tr_head = (m_tr_head + 1) % TRACE;
		if (m_tr_count < TRACE) m_tr_count++;
	}
	if (fetch) {
		// counting first: a counter gated by this cycle's own event starts with the next fetch
		if (gp_source() == 3 && counting) clock_gp(gp_gate());
	}
	bool en1 = (m_c2 & 0x07) != 0, en2 = (m_c2 & 0x0a) != 0;   // LIM (02) uses both events, ARM (04) event 1
	bool m1 = en1 && match(0, a, d, cyc, probe), m2 = en2 && match(1, a, d, cyc, probe);
	// the G.P. counter counts an event's comparisons (CNT 1/2) whether or not C2 lets that event
	// break (the 8002 diagnostics' hardware analyzer test counts event 2 with C2 = 01, event 1
	// with C2 = 08); an enabled event is counted in occurrence() as before
	if (counting && gp_source() == 0 && !en1 && match(0, a, d, cyc, probe)) clock_gp(gp_gate());
	if (counting && gp_source() == 1 && !en2 && match(1, a, d, cyc, probe)) clock_gp(gp_gate());
	if (trace && (m1 || m2)) fprintf(stderr, "[%.4f RTPA match%s%s at %04x d=%02x cyc=%02x pc=%04x]\n", clock ? *clock : 0.0, m1 ? " E1" : "", m2 ? " E2" : "", a, d, cyc, m_cur_pc);
	bool hit = false;
	if (m_c2 & 0x02) {                        // LIM: both events in the same cycle (channel 1's pass/delay apply)
		// "If the LIMit requirement is on, R1 (EVT 2 address and Control bits) will also be ANDed
		// with R0 and R2 for EVT 1" (070-2356 §2, step 4b): event 2's data and probe do not take
		// part (the 8002 diagnostics' TRACE ALL TEST limits A <= 0017, D = C3 by A >= 0014 with
		// D <= C2 and expects the break at the C3 fetch, event 2 absent from the status)
		if (m1 && addr_match(1, a) && ctl_match(1, cyc)) occurrence(0, hit);
		return;
	}
	// under ARM, event 2 counts only on a cycle after the arming one: "EVT RST is generated on
	// the next bus cycle, resetting both event F.F.s" (070-2356 §2); 070-2785 3.B.6 arms and
	// breaks on the same address, and breaks one loop later
	const bool armed_before = m_armed;
	if (m1) occurrence(0, hit);
	if (m2 && (!(m_c2 & 0x04) || armed_before)) occurrence(1, hit);
}

void rtpa::instr_end(u16 next_pc, int tstates, double hz)
{
	// time-based G.P. sources: emulator clock, and a 1 MHz time base -- usec directly,
	// msec through channel 2's counter 1 (DOS/50 loads it with 1000)
	if (!m_stopped && (m_c3 & 0x08)) {
		int src = gp_source();
		// the clocks of this instruction the G.P. counter sees: all of them, or with known cycle
		// lengths only those between the gate opening and the break
		int lo = 0, hi = tstates; bool gate = gp_gate() || m_gate_before;   // an instruction that disarms counts whole, as one that arms
		if (states_fetch) {
			lo = m_gate_before ? 0 : m_open_at; hi = m_stop_at >= 0 ? std::min(m_stop_at, tstates) : tstates;
			if (lo < 0 || hi < lo) lo = hi = 0;
			gate = true;
		}
		const int n = hi - lo;
		if (src == 5) for (int i = 0; i < n; i++) clock_gp(gate);
		// usec and msec: edges of the free-running 1 us time base, and outputs of the msec divider,
		// that fall within the counted part of this instruction (absolute time, in us)
		const double t_lo = m_abs_us + lo * 1e6 / hz, t_hi = m_abs_us + hi * 1e6 / hz;
		if (src == 7) for (long k = long(std::floor(t_hi)) - long(std::floor(t_lo)); k > 0; k--) clock_gp(gate);
		if (src == 6) {
			const double per = m_pit[1].c[1].cr ? m_pit[1].c[1].cr : 65536;
			const double first = m_msec_load_us + per - 1;   // the output pulse, at count 1
			auto pulses = [&](double t) { return t < first ? 0L : long(std::floor((t - first) / per)) + 1; };
			for (long k = pulses(t_hi) - pulses(t_lo); k > 0; k--) clock_gp(gate);
		}
		m_abs_us += tstates * 1e6 / hz;
	}
	// the PC latches follow every instruction while the analyser runs, and hold the breaking one:
	// TEKDOS, with the board fitted, shows a halt from them (070-2785 3.B.4: "0301 76 HLT ...
	// 0301 BREAK" after a run in which the RTPA never triggers)
	if (!m_stopped && (m_c3 & 0x08)) { m_pc_last = m_cur_pc; m_pc_next = next_pc; }
	if (m_thaw) { m_thaw = false; m_frozen = false; }
	if (!m_trig || m_stopped) return;
	m_pc_last = m_cur_pc; m_pc_next = next_pc;
	if (stop_status_08) m_status |= 0x08;   // see rtpa.h
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
