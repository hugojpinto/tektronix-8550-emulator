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
#include <cstdio>
#include <cstdlib>
#include <functional>

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
