// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Trigger Trace Analyzer (TTA) option on the 8301: ports 70-77 and a RAM
// window on the system bus (E800-F9FF). Reverse-engineered from TTA.TST V1.3A
// (8550_DIAG.IMD, Select mode 6); see notes/tta.md. Counters: an AMD Am9513
// System Timing Controller (port of MAME's device) at 76 (data) / 77 (command),
// clocked at 5 MHz (J6165, "normal operation") from the 8301's 20 MHz oscillator.
#ifndef TEK_TTA_H
#define TEK_TTA_H
#include "emu.h"
#include "machine/am9513.h"
#include "machine/tta_run.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

class tta
{
public:
	tta() { stc.start(); }
	bool present = false;
	bool trace = getenv("TEK_TTATRACE") != nullptr;
	double *clock = nullptr;

	// ports 70-77; now = 2650 clocks (tek8301::now_cycles), for the counter clock
	u8 read(int r, u64 now);
	void write(int r, u8 d, u64 now);
	// the RAM window: system-bus addresses E800-F9FF while the RAM is selected
	bool ram_selected() const { return present && (m_reg[0] & 0x02); }
	bool ram_decodes(u16 a) const { return ram_selected() && a >= 0xe800 && a < 0xfa00; }
	u8 ram_read(u16 a);
	void ram_write(u16 a, u8 d);

	// time passes (2650 clocks): the Am9513 runs from X1 = 5 MHz while port 70 bit 2 is set
	void sync(u64 now);
	// a 2650 access to the RAM window: in test mode (port 70 = x7) the comparators
	// look up its row and latch the events (see tta.cpp)
	bool watching() const { return present && (m_reg[0] & 0x03) == 0x03; }
	void ram_access(u64 now, u16 a);
	bool take_irq() { bool i = m_irq; m_irq = false; return i; }
	bool requesting() const { return present && m_req; }

	// ---- run mode: the board watches the program under test on the emulator processor
	// (port 70 = x5: comparators on, clock running, RAM not selected; inferred). Modelled
	// functionally by tta_run (see tta_run.h): the run-mode encoding of the comparator RAMs
	// and the TTA software's Am9513 programming are not known.
	tta_run run;
	bool target_watch() const { return present && (m_reg[0] & 0x07) == 0x05; }
	void run_start(bool reset_counters, u64 t) { m_brk_status = 0; run.run_start(reset_counters, t); }
	void instr_begin(u64 t, u16 pc) { run.instr_begin(pc, t); }
	enum cycle_t { FETCH = tta_run::FETCH, MREAD = tta_run::MREAD, MWRITE = tta_run::MWRITE, IOREAD = tta_run::IOREAD, IOWRITE = tta_run::IOWRITE };
	void target_bus(u64 t, u32 a, u8 d, cycle_t c) { run.cycle(a, d, tta_run::cycle_t(c), t); }
	void instr_end(u16 pc, u64 t) { run.instr_end(pc, t); if (run.take_stop()) { m_stopped = true; m_break = true; m_brk_status = u8(0x18 | (run.last_stop_trig() & 3)); } }
	bool stopped() const { return m_stopped; }
	// the last run's break as DOS/50 V1.0 reads it at C2 (see tek8301::io_read): latched until the next run
	u8 break_status() const { return m_brk_status; }
	void release() { m_stopped = false; }
	bool take_break() { bool b = m_break; m_break = false; return b; }
	void port_write(int r, u8 d) { write(r, d, m_last); }   // for the command software
	u8 port_read(int r) { return read(r, m_last); }

	// ---- run mode at register level (port 70 = 01: comparators on, test bit 2 clear), as the
	// 8086 diagnostic's TTA interface test (module 70) programs it; the emulator board hands
	// over each of the program's bus cycles in the TTA's acquisition format (notes/tta.md)
	struct sample {
		u32 addr;        // A23-0 (pages 6F, 6E, 6D)
		u16 data;        // D15-0 (pages 6C, 6B)
		u8 bus;          // the emulator's bus status (page 69); for the 8086: notes/8086.md
		u8 activity;     // page 68
	};
	bool target_registers() const { return present && (m_reg[0] & 0x05) == 0x01; }
	void target_cycle(u64 now, const sample &s);
	bool take_target_break() { bool b = m_tbreak; m_tbreak = false; return b; }

	am9513_device stc{ 5'000'000 };
	// the P6451 data acquisition probe's eight lines; unset = nothing clipped on (read high)
	std::function<u8()> probe;

private:
	u8 m_reg[8] = {};
	u8 m_ram[0x1200] = {};                    // pages E8-F9
	u8 m_trig = 0;                            // port 73: trigger latches 4-1 (also CPU-writable)
	u8 m_armed = 0;
	bool m_irq = false;                       // INT 29 edge (taken by the 8301)
	bool m_req = false;                       // the request flip-flop, read in F2 bit 2
	u8 m_acq_ptr = 0;                         // next acquisition sample (port 75)
	bool m_stopped = false, m_break = false;
	u8 m_brk_status = 0;
	bool m_tbreak = false;                    // a trigger with its break enabled (port 72 bit 4+k)
	void triggers(u8 ev, int row_or_minus1);
	u8 m_ev = 0;                              // event outputs 4-1, latched at each RAM access in test mode
	void set_events(u8 ev);                           // trigger N has armed trigger N+1 (cyclic)
	u64 m_last = 0; u32 m_half = 0;           // counter clock: 5 MHz = 2.5 X1 per 2650 clock
	u8 page(int p, int row) const { return m_ram[((p - 0x68) << 8) | row]; }   // CPU page 68-79
	bool ctr_out(int c) const;
	void src_w(int n, int l);                 // Am9513 SRC n+1 / GATE n+1 levels
	void gate_w(int n, int l);                // counter c's OUT pin (pulled up when high impedance)
	void log(const char *w, int r, u8 v) { if (trace) fprintf(stderr, "[%.7f TTA %s %02x %02x]\n", clock ? *clock : 0.0, w, 0x70 + r, v); }
};
#endif
