// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA command set (Trigger Trace Analyzer users manual 070-3760-01, section 2): acq,
// ad, bre, bus, cons, cou, ctr, data, disp, eve, pro, qua, tclr, ts. DOS/50 V1.0 predates
// the TTA, and the later DOS/50 that had these commands does not survive, so the emulator
// runs them itself: lines typed at the DOS/50 prompt whose first word is one of them come
// here instead (tek8550_system::key_in).
//
// The commands drive the board at register level as the 8540's OS/40 TTA software does
// (ROMs 160-1381/1382, which DOS/50 V2 shared; notes/tta-os40.md): they write the
// comparator RAMs, program the Am9513 with OS/40's command sequences and set ports 70-72;
// at GO, at the end of a run and at a TTA break they do what OS/40's resident TTA module
// did (tek8301::tta_sw_go, tta_sw_stop, tta_sw_break); disp reads the acquisition memory.
// Only what they print is the emulator's: the users manual's displays.
#ifndef TEK_SYSTEM_TTA_CONSOLE_H
#define TEK_SYSTEM_TTA_CONSOLE_H
#include "emu.h"
#include "system/tta_eve.h"
#include <string>
#include <vector>

class tek8550_system;
class tta;

class tta_console
{
public:
	explicit tta_console(tek8550_system &s);
	std::string run(const std::string &line);     // one command line; returns what it prints
	static bool is_command(const std::string &word);

	// ---- the settings, as the commands give them (OS/40 keeps the same in its channel blocks)
	using field = tta_field;                   // a value, a value with don't-care bits, or a range
	// bus symbols (the 8085A set the manual uses; the Z80 board takes the same ones)
	enum bus_t : u8 { B_F = 1, B_NF = 2, B_M = 4, B_I = 8, B_RD = 16, B_WT = 32 };
	struct event_def {
		field addr, data;
		u8 bus = 0;                            // bus_t, ANDed; 0 = don't care
		u8 probe_val = 0, probe_care = 0;
		u8 ctr_val = 0, ctr_care = 0;          // counter outputs, bit 0 = counter 1
		int qual = -1;                         // event qualifier input: -1 don't care
	};
	enum out_t : u8 { O_NONE, O_ARM, O_DISARM, O_PULSE, O_DELAY, O_TIMEOUT };
	enum gate_t : u8 { G_NONE, G_OFF, G_CTR, G_TRIGH, G_TRIGL, G_SEQH, G_SEQL, G_SELF };
	enum src_t : u8 { S_NONE, S_200NS, S_2US, S_20US, S_200US, S_2MS, S_EV1, S_EV2, S_EV3, S_EV4,
		S_TRIG1, S_TRIG2, S_TRIG3, S_TRIG4, S_ACQ, S_CYC, S_EMUCLK, S_QUA };
	struct counter {
		bool prog = false;                     // programmed by cou
		out_t mode = O_NONE;
		bool has_value = false; u32 init = 0;
		src_t src = S_NONE;
		gate_t gate = G_NONE;
		int restart = -1;                      // -1 not set, 0 off, 1 on
		bool down() const { return mode == O_PULSE || mode == O_DELAY || mode == O_TIMEOUT; }
	};
	enum brk_t : u8 { BRK_NONE, BRK_STOP, BRK_CONT };
	enum cons_t : u8 { C_CYC, C_FET, C_EMU };

private:
	tek8550_system &sys;
	bool m_header = false;
	event_def m_ev[4];
	counter m_ctr[4];
	brk_t m_brk[4] = {};
	cons_t m_cons_mode = C_CYC;
	std::vector<std::vector<int>> m_cons;      // sequences of channels 0..3
	bool m_acq_all = true;
	bool m_acq_for = false, m_acq_after4 = false; u32 m_acq_n = 0; src_t m_acq_src = S_NONE;
	// OS/40's channel blocks (410E + 1A k; counter 5 = acquisition): output type (0 arm,
	// 1 disarm, 2 timeout, 3 delay, 4 pulse), Am9513 mode, load and hold registers
	struct block { u8 type = 0; u16 mode = 0x0b0c, load = 0, hold = 0; } m_blk[5];
	u8 m_reprogram = 0;                        // 410D: counters reprogrammed at GO with an address
	u16 m_last_pc = 0;
	int m_filled = 0;                          // acquisitions stored since power-up (at most 255)

	tta &board() const;
	u8 port(int r) const;
	void port_w(int r, u8 d) const;
	void ram_w(int page, int row, u8 d) const;
	u8 ram(int page, int row) const;
	// programming the board
	void init();
	void apply_event(int k);
	void apply_counter(int c, bool now);
	void counter_sequence(int c);
	void apply_breaks();
	void apply_cons();
	void apply_acq();
	void src5_select();
	void clear_channel(int k);
	void clear_all();
	// OS/40's resident TTA module
	void go(bool new_address);
	void stop();
	bool on_break(u8 trig, u16 pc);
	int run_samples() const;                   // acquisitions of the current run (the run marker)

	std::string command(const std::string &line);
	std::string status(u8 which, bool events, bool counters) const;
	std::string display(int n, bool since_start) const;
	std::string cons_text() const;
	std::string acq_text(bool with_remaining) const;
	std::string show_bre() const;
	void trace_line(u16 pc);
};
#endif
