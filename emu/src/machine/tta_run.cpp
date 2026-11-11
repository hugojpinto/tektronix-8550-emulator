// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA in run mode -- see tta_run.h. Section numbers refer to the TTA users manual
// 070-3760-01; [OS/40] to the 8540 System Users Manual 070-3939, which describes the same
// commands and fills some of the TTA manual's gaps.
#include "machine/tta_run.h"
#include <algorithm>

// Counter outputs while counting, at the terminal value and after it ([OS/40] Table 5-2):
// ARM 1 1 1, DISARM 0 0 0, PULSE 0 1 0, DELAY 0 0 1, TIMEOUT 1 1 0. A counter that cou
// has not programmed holds its output high, so the trigger follows the event (Display 2-1).
bool tta_run::counter::output() const
{
	if (!prog) return true;
	switch (mode) {
	case O_DISARM: return false;
	case O_PULSE: return at_tc;
	case O_DELAY: return done;
	case O_TIMEOUT: return !done || at_tc;
	case O_ARM: default: return true;
	}
}

std::string tta_run::bus_symbols(cycle_t t)
{
	switch (t) {
	case FETCH: return "M RD F";
	case MREAD: return "M RD";
	case MWRITE: return "M WT";
	case IOREAD: return "I RD";
	case IOWRITE: return "I WT";
	}
	return "";
}

void tta_run::run_start(bool reset_counters, u64 t)
{
	m_t = t;
	m_new_run = true;
	if (on_start) on_start();
	m_trig_prev = 0; m_raw_prev = 0; m_prev_valid = false;
	m_cons_state.assign(cons.size(), 0);
	// "G entered with an address reinitializes all the counters, including the one
	// associated with the Acquisition Memory" [OS/40]
	if (reset_counters) {
		for (counter &c : ctr) if (c.prog) c.load();
		m_acq_left = acq_n; m_acq_done = false; m_acq_started = acq_for && !acq_after4;
	}
}

void tta_run::instr_begin(u16 pc, u64 t)
{
	if (m_t == 0) m_t = t;
	m_pc = pc;
	m_tr_instr = 0;
}

// consecutive events (cons, §2-16; [OS/40] 5-17): in a sequence only the last event can
// produce a trigger, and only when each earlier event occurred on the preceding cycle of
// the selected kind; otherwise detection starts over from the first event.
u8 tta_run::effective_events(const u8 raw, cycle_t t)
{
	u8 eff = raw;
	if (m_cons_state.size() != cons.size()) m_cons_state.assign(cons.size(), 0);
	const bool qualifying = cons_mode != C_FET || t == FETCH;
	for (size_t i = 0; i < cons.size(); i++) {
		const std::vector<int> &s = cons[i];
		const int n = int(s.size());
		int &st = m_cons_state[i];
		for (int k : s) eff &= u8(~(1 << k));
		if (!qualifying) continue;
		const int last = s[size_t(n - 1)];
		bool fired = st == n - 1 && ((raw >> last) & 1);
		if (fired) eff |= u8(1 << last);
		if (!fired && st > 0 && st < n - 1 && ((raw >> s[size_t(st)]) & 1)) st++;
		else st = ((raw >> s[0]) & 1) ? 1 : 0;
	}
	return eff;
}

int tta_run::source_pulses(src_t s, u8 eff, u8 trig, bool stored, double, double emuclk)
{
	switch (s) {
	case S_EV1: case S_EV2: case S_EV3: case S_EV4:   // "the event signal ANDed with cyc" (Table 2-1; §4, SN B030000+)
		return (eff >> (s - S_EV1)) & 1;
	case S_TRIG1: case S_TRIG2: case S_TRIG3: case S_TRIG4: {   // a trigger counts once per occurrence
		int k = s - S_TRIG1;
		return ((trig >> k) & 1) && !((m_trig_prev >> k) & 1);
	}
	case S_ACQ: return stored ? 1 : 0;
	case S_CYC: return 1;
	case S_EMUCLK: return int(emuclk);
	default: return 0;
	}
}

void tta_run::count(int k, int pulses)
{
	counter &c = ctr[k];
	if (!c.running || c.done || pulses <= 0) return;
	if (c.down()) {
		c.cur = c.cur > u32(pulses) ? c.cur - u32(pulses) : 0;
		if (c.cur == 0) { c.done = true; c.at_tc = true; }
	} else {
		c.cur = std::min<u32>(c.cur + u32(pulses), 65535);
		if (c.cur == 65535) { c.done = true; c.at_tc = true; }
	}
}

void tta_run::store(const sample &s)
{
	sample v = s;
	v.first = m_new_run; m_new_run = false;
	m_ring[m_head] = v;
	m_head = (m_head + 1) % 255;
	if (m_count < 255) m_count++;
}

void tta_run::cycle(u32 addr, u8 data, cycle_t type, u64 t)
{
	const double dt_ns = t > m_t ? double(t - m_t) * 1e9 / hz : 0;
	const double emuclk = t > m_t ? double(t - m_t) / 2 : 0;   // Z80: the emulator clock divided by two (§3-8)
	m_t = t;

	// ---- the four event comparators (an undefined part, or event, is don't care)
	u8 outs = 0;
	for (int k = 0; k < 4; k++) if (ctr[k].output()) outs |= u8(1 << k);
	const bool fetch = type == FETCH, mem = type & (FETCH | MREAD | MWRITE), io = type & (IOREAD | IOWRITE);
	const bool rd = type & (FETCH | MREAD | IOREAD), wt = type & (MWRITE | IOWRITE);
	u8 raw = 0;
	for (int k = 0; k < 4; k++) {
		const event_def &d = ev[k];
		if (!d.addr.match(addr) || !d.data.match(data)) continue;
		if ((d.bus & B_F) && !fetch) continue;
		if ((d.bus & B_NF) && fetch) continue;
		if ((d.bus & B_M) && !mem) continue;
		if ((d.bus & B_I) && !io) continue;
		if ((d.bus & B_RD) && !rd) continue;
		if ((d.bus & B_WT) && !wt) continue;
		if ((probe ^ d.probe_val) & d.probe_care) continue;
		if ((outs ^ d.ctr_val) & d.ctr_care) continue;
		if (d.qual == 1) continue;                     // the EVENT QUALIFIER input is not driven (low)
		raw |= u8(1 << k);
	}
	const u8 eff = effective_events(raw, type);
	bool cons4 = false;                                // event 4 completed a sequence this cycle
	for (const std::vector<int> &s : cons) if (s.back() == 3 && ((eff >> 3) & 1)) cons4 = true;

	// ---- acquisition (acq, §2-4): all transactions, or event 4's; FOR stops it after a count
	const sample here{ addr, data, probe, type, false };
	int stored = 0;
	if (!m_acq_done) {
		if (acq_mode == A_ALL || ((eff >> 3) & 1)) {
			// with acq ev4 and a sequence ending in event 4, the qualifying cycle is stored too
			// (Display 1-14 lists each ADD M fetch before its operand read)
			if (acq_mode == A_EV4 && cons4 && m_prev_valid) { store(m_prev); stored++; }
			store(here); stored++;
		}
	}
	m_prev = here; m_prev_valid = true;

	// ---- counters, then triggers (trigger N = event N AND counter N output)
	auto gate_open = [&](int k, u8 trig) {
		const counter &c = ctr[k];
		if (k == 0 || c.gate == G_NONE || c.gate == G_OFF) return true;
		int j = k - 1;
		bool now = (trig >> j) & 1, before = (m_trig_prev >> j) & 1;
		switch (c.gate) {
		case G_CTR: return ctr[j].at_tc;
		case G_TRIGH: return now;
		case G_TRIGL: return !now;
		case G_SEQH: return c.started || (now && !before);
		case G_SEQL: return c.started || (!now && before);
		case G_SELF: return bool((m_trig_prev >> k) & 1);
		default: return true;
		}
	};
	auto step_counters = [&](u8 trig, bool trig_sources) {
		for (int k = 0; k < 4; k++) {
			counter &c = ctr[k];
			if (!c.prog || !c.running) continue;
			const bool ts = c.src >= S_TRIG1 && c.src <= S_TRIG4;
			const bool tg = c.gate == G_TRIGH || c.gate == G_TRIGL || c.gate == G_SEQH || c.gate == G_SEQL || c.gate == G_SELF;
			if ((ts || tg) != trig_sources) continue;
			c.at_tc = false;
			if (k > 0 && c.restart == 1 && c.gate != G_NONE && c.gate != G_OFF && c.gate != G_CTR) {
				int j = k - 1; bool now = (trig >> j) & 1, before = (m_trig_prev >> j) & 1;
				bool asserted = (c.gate == G_TRIGL || c.gate == G_SEQL) ? (!now && before) : (now && !before);
				if (asserted && c.started) c.load();          // restart: reload the initial value
			}
			if (!gate_open(k, trig)) continue;
			if (c.gate == G_SEQH || c.gate == G_SEQL) c.started = true;
			int p = 0;
			if (c.src >= S_200NS && c.src <= S_2MS) {          // the time bases: 200 ns ... 2 ms
				static const double unit[] = { 200, 2000, 20000, 200000, 2000000 };
				c.tb += dt_ns / unit[c.src - S_200NS];
				p = int(c.tb); c.tb -= p;
			} else p = source_pulses(c.src, eff, trig, stored > 0, dt_ns, emuclk);
			count(k, p);
		}
	};
	step_counters(0, false);
	u8 trig = 0;
	for (int k = 0; k < 4; k++) if (((eff >> k) & 1) && ctr[k].output()) trig |= u8(1 << k);
	step_counters(trig, true);

	// acquisition FOR counting (acq ... for n source [aftertrig4])
	if (acq_for && !m_acq_done) {
		if (!m_acq_started && acq_after4 && ((trig >> 3) & 1)) m_acq_started = true;
		if (m_acq_started) {
			int p;
			if (acq_src >= S_200NS && acq_src <= S_2MS) {
				static const double unit[] = { 200, 2000, 20000, 200000, 2000000 };
				m_acq_tb += dt_ns / unit[acq_src - S_200NS]; p = int(m_acq_tb); m_acq_tb -= p;
			} else p = source_pulses(acq_src, eff, trig, stored > 0, dt_ns, emuclk);
			m_acq_left = m_acq_left > u32(p) ? m_acq_left - u32(p) : 0;
			if (m_acq_left == 0) m_acq_done = true;
		}
	}
	m_tr_instr |= trig;
	m_trig_prev = trig;
	m_raw_prev = raw;
}

void tta_run::instr_end(u16, u64)
{
	// breaks take effect after the instruction that held the triggering cycle (§1-10)
	u8 stop = 0, cont = 0;
	for (int k = 0; k < 4; k++)
		if ((m_tr_instr >> k) & 1) { if (brk[k] == BRK_STOP) stop |= u8(1 << k); else if (brk[k] == BRK_CONT) cont |= u8(1 << k); }
	m_tr_instr = 0;
	if (stop) { m_stop = true; m_stop_trig = stop; m_trig_prev = 0; }   // the triggers go low when the emulator halts
	if ((stop || cont) && on_break) on_break(u8(stop | cont), m_pc, stop != 0);
}

std::vector<tta_run::sample> tta_run::samples(int last_n, bool since_start) const
{
	std::vector<sample> v;
	for (int i = m_count; i > 0; i--) v.push_back(m_ring[(m_head - i + 255 * 2) % 255]);
	if (since_start) {
		size_t from = 0;
		for (size_t i = 0; i < v.size(); i++) if (v[i].first) from = i;
		v.erase(v.begin(), v.begin() + long(from));
	}
	if (last_n >= 0 && int(v.size()) > last_n) v.erase(v.begin(), v.end() - last_n);
	return v;
}
