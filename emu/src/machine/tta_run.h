// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA in run mode: watching the program under test on the emulator processor.
//
// TTA.TST, the only software we have for the board, drives it in test mode (the 2650 as the
// watched bus); see tta.h and notes/tta.md. How the target's signals address the comparator
// RAMs in run mode, and how the TTA software programmed the Am9513 for each `cou` option,
// is not known. This engine therefore models run mode at the level the TTA users manual
// (070-3760-01) and the 8540 OS/40 manual describe it: four channels of event (address,
// data, bus, probe, counter outputs, qualifier), counter and trigger; consecutive events;
// breaks; and the 255-transaction acquisition memory. The two manuals' statements it rests
// on are quoted in docs/ARCHITECTURE.md §8.
#ifndef TEK_MACHINE_TTA_RUN_H
#define TEK_MACHINE_TTA_RUN_H
#include "emu.h"
#include <functional>
#include <string>
#include <vector>

class tta_run
{
public:
	// bus cycle types of the emulator processor
	enum cycle_t : u8 { FETCH = 1, MREAD = 2, MWRITE = 4, IOREAD = 8, IOWRITE = 16 };

	// a value, a value with don't-care bits, or an inclusive range; optionally excluded
	struct field {
		bool on = false, negate = false, masked = false;
		u32 lo = 0, hi = 0, care = 0;          // masked: (x & care) == (lo & care)
		bool match(u32 x) const {
			if (!on) return true;
			bool m = masked ? ((x ^ lo) & care) == 0 : (x >= lo && x <= hi);
			return m != negate;
		}
	};
	// bus symbols (manual §2 bus; the 8085A set, used for the Z80 too): f nf m i rd wt
	enum bus_t : u8 { B_F = 1, B_NF = 2, B_M = 4, B_I = 8, B_RD = 16, B_WT = 32 };
	struct event_def {
		field addr, data;
		u8 bus = 0;                            // bus_t, ANDed; 0 = don't care
		u8 probe_val = 0, probe_care = 0;
		u8 ctr_val = 0, ctr_care = 0;          // counter outputs, bit 0 = counter 1
		int qual = -1;                         // event qualifier input: -1 don't care
		bool any() const { return addr.on || data.on || bus || probe_care || ctr_care || qual >= 0; }
	};

	enum out_t : u8 { O_NONE, O_ARM, O_DISARM, O_PULSE, O_DELAY, O_TIMEOUT };
	enum gate_t : u8 { G_NONE, G_OFF, G_CTR, G_TRIGH, G_TRIGL, G_SEQH, G_SEQL, G_SELF };
	enum src_t : u8 { S_NONE, S_200NS, S_2US, S_20US, S_200US, S_2MS, S_EV1, S_EV2, S_EV3, S_EV4,
		S_TRIG1, S_TRIG2, S_TRIG3, S_TRIG4, S_ACQ, S_CYC, S_EMUCLK, S_QUA };
	struct counter {
		bool prog = false;                     // programmed by cou (else: output high)
		out_t mode = O_NONE;
		bool has_value = false; u32 init = 0;
		src_t src = S_NONE;
		gate_t gate = G_NONE;
		int restart = -1;                      // -1 not set, 0 off, 1 on
		// live state
		u32 cur = 0; bool running = false, done = false, at_tc = false, started = false;
		double tb = 0;                         // timebase phase, in that timebase's units
		bool output() const;
		bool down() const { return mode == O_PULSE || mode == O_DELAY || mode == O_TIMEOUT; }
		void load() { cur = has_value ? init : 0; running = true; done = false; at_tc = false; started = false; tb = 0; }
	};

	enum brk_t : u8 { BRK_NONE, BRK_STOP, BRK_CONT };
	enum cons_t : u8 { C_CYC, C_FET, C_EMU };
	enum acq_t : u8 { A_ALL, A_EV4 };

	// ---- programming (the TTA commands)
	event_def ev[4];
	counter ctr[4];
	brk_t brk[4] = {};
	cons_t cons_mode = C_CYC;
	std::vector<std::vector<int>> cons;        // sequences of channels 0..3
	acq_t acq_mode = A_ALL;
	bool acq_for = false, acq_after4 = false; u32 acq_n = 0; src_t acq_src = S_NONE;
	void clear_channel(int k) { ev[k] = event_def(); ctr[k] = counter(); brk[k] = BRK_NONE; }
	void clear_all() { for (int k = 0; k < 4; k++) clear_channel(k); cons.clear(); cons_mode = C_CYC; acq_mode = A_ALL; acq_for = acq_after4 = false; acq_n = 0; acq_src = S_NONE; }
	void program_counter(int k) { ctr[k].load(); }   // after cou: load the initial value

	// ---- the watched processor: time is its clock in T-states (hz per second)
	void run_start(bool reset_counters, u64 t);
	void instr_begin(u16 pc, u64 t);
	void cycle(u32 addr, u8 data, cycle_t type, u64 t);
	void instr_end(u16 next_pc, u64 t);
	double hz = 4e6;

	// results
	bool take_stop() { bool s = m_stop; m_stop = false; return s; }
	u8 last_stop_trig() const { return m_stop_trig; }
	std::function<void(u8 trig, u16 pc, bool stop)> on_break;   // after the instruction
	std::function<void()> on_start;                              // a run of the program begins
	u32 acq_remaining() const { return m_acq_left; }
	bool acq_counting() const { return acq_for && m_acq_started; }
	u8 probe = 0x00;                           // P7-P0: nothing clipped on reads 0000 0000 (Display 1-2)
	u16 last_pc() const { return m_pc; }       // the instruction of the last break

	struct sample { u32 addr; u8 data, probe; cycle_t type; bool first; };
	std::vector<sample> samples(int last_n = 255, bool since_start = false) const;
	void clear_samples() { m_count = 0; m_head = 0; }
	static std::string bus_symbols(cycle_t t);

private:
	u8 effective_events(const u8 raw, cycle_t t);
	void count(int k, int pulses);
	int source_pulses(src_t s, u8 raw, u8 trig, bool stored, double dt_ns, double emuclk);
	void store(const sample &s);
	u8 m_trig_prev = 0, m_tr_instr = 0, m_raw_prev = 0;
	u16 m_pc = 0;
	std::vector<int> m_cons_state;
	bool m_stop = false; u8 m_stop_trig = 0;
	u64 m_t = 0;                               // last cycle's time
	bool m_new_run = false;
	// acquisition: a ring of 255 samples
	sample m_ring[255]; int m_head = 0, m_count = 0;
	bool m_acq_started = false, m_acq_done = false; u32 m_acq_left = 0; double m_acq_tb = 0;
	sample m_prev{}; bool m_prev_valid = false;
};
#endif
