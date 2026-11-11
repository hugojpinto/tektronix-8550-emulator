// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Trigger Trace Analyzer (TTA) option on the 8301: ports 70-77 and a RAM
// window on the system bus (E800-F9FF). Reverse-engineered from TTA.TST V1.3A
// (8550_DIAG.IMD, Select mode 6; notes/tta.md), and for run mode from the 8540's OS/40
// TTA software (ROMs 160-1381/1382; notes/tta-os40.md) and the 8086 diagnostic's module
// 70. Counters: an AMD Am9513 System Timing Controller (port of MAME's device) at 76
// (data) / 77 (command), clocked at 5 MHz (J6165, "normal operation") from the 8301's
// 20 MHz oscillator.
//
// The board has two ways of being clocked:
// - test mode (port 70 bit 2): every 2650 access to a comparator page is a "cycle", and
//   every comparator RAM is addressed by the 2650's A7-0 (TTA.TST);
// - run mode (port 70 bit 0 with bit 2 clear): each bus cycle of the program under test on
//   the emulator processor is one, and the comparator RAMs are addressed by the target's
//   address, data, probe and control lines (target_cycle).
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

	// time passes (2650 clocks): in test mode the Am9513 runs from X1 = 5 MHz
	void sync(u64 now);
	// a 2650 access to the RAM window: with the comparators on it looks up a row in every
	// comparator RAM and latches the events (see tta.cpp)
	bool watching() const { return present && (m_reg[0] & 0x03) == 0x03; }
	void ram_access(u64 now, u16 a);
	bool take_irq() { bool i = m_irq; m_irq = false; return i; }
	bool requesting() const { return present && m_req; }

	// ---- run mode: the comparators watch the program under test on the emulator processor
	// (port 70 bit 0 set, test bit 2 clear: OS/40 7325 sets it at the start of a run, 7480
	// clears it at the end; the 8086 diagnostic's module 70 writes 01)
	struct sample {
		u32 addr;        // A23-0 (acquisition pages 6F, 6E, 6D)
		u16 data;        // D15-0 (pages 6C, 6B)
		u8 bus;          // the emulator's bus status byte (page 69; comparator page 70's Y input)
		u8 ext;          // bus-extension signals in bits 7-5 (page 68; page 70's X input)
		bool fetch;      // an instruction fetch (for consecutive events counted in fetches)
	};
	bool target_watch() const { return present && (m_reg[0] & 0x05) == 0x01; }
	// the 8-bit emulator boards' cycles. Their bus status byte is the 8085 support's (DOS/50
	// V2.1 EMUMON1 tables: bit 0 fetch, bit 1 read, bit 2 memory; notes/tta-os40.md §4); the
	// Z80 board is taken to drive the same (inferred: TNIX lists the same symbols for it)
	enum cycle_t : u8 { FETCH = 0x07, MREAD = 0x06, MWRITE = 0x04, IOREAD = 0x02, IOWRITE = 0x00 };
	static sample cycle8(u16 a, u8 d, cycle_t c) { return sample{ a, d, u8(c), 0, c == FETCH }; }
	// one bus cycle; cyc counts the emulator processor's clock at hz
	void target_cycle(u64 cyc, double hz, const sample &s);
	// a trigger with its break enabled (port 72 bits 3-0) occurred since the last call
	bool take_target_break(u8 &trig) { trig = m_tbreak; m_tbreak = 0; return trig != 0; }
	// the board holds the emulator processor after a break (SLV OPREQ, as the RTPA does)
	void hold(u8 trig) { m_stopped = true; m_brk_status = u8(0x18 | (trig & 3)); }
	bool stopped() const { return m_stopped; }
	void release() { m_stopped = false; }
	// the last run's break as DOS/50 V1.0 reads it at C2 (see tek8301::io_read): an emulator
	// bridge, latched until the TTA software starts the next run
	u8 break_status() const { return m_brk_status; }
	void clear_break_status() { m_brk_status = 0; }
	// the counter outputs OUT5-OUT1 as the pins are (bit 0 = OUT1; high impedance reads high)
	u8 outputs() const;
	// the four event comparators' outputs (bit 0 = event 1) for these inputs (tta.cpp)
	u8 comparators(u32 a, u16 d, u8 probe, u8 x, u8 y) const;
	// the TTA software's port accesses (system/tta_console), at the board's current time
	void port_write(int r, u8 d) { write(r, d, m_last); }
	u8 port_read(int r) { return read(r, m_last); }

	am9513_device stc{ 5'000'000 };
	// the P6451 data acquisition probe's eight lines; unset = nothing clipped on (reads
	// 0000 0000, as the users manual's displays show: Display 1-2)
	std::function<u8()> probe;

private:
	u8 m_reg[8] = {};
	u8 m_ram[0x1200] = {};                    // pages E8-F9
	u8 m_trig = 0;                            // port 73: trigger latches 4-1 (also CPU-writable)
	u8 m_armed = 0;                           // triggers armed by their predecessor (port 71 links)
	bool m_irq = false;                       // INT 29 edge (taken by the 8301)
	bool m_req = false;                       // the request flip-flop, read in F2 bit 2
	u8 m_acq_ptr = 0;                         // next acquisition sample (port 75)
	bool m_stopped = false;
	u8 m_brk_status = 0;
	u8 m_tbreak = 0;                          // triggers that asked for a break (port 72 bits 3-0)
	u8 m_ev = 0;                              // event outputs 4-1, latched at each RAM access in test mode
	u8 m_tr_level = 0;                        // run mode: the triggers on the last cycle
	u64 m_last = 0; u32 m_half = 0;           // test-mode counter clock: 2.5 X1 per 2650 clock
	u64 m_tcyc = 0; double m_thz = 0, m_tx1 = 0, m_temu = 0;   // run-mode time: X1 and EMUCLK phases
	u8 m_prev_rec[8] = {}; bool m_prev_stored = true;
	u8 page(int p, int row) const { return m_ram[((p - 0x68) << 8) | row]; }   // CPU page 68-79
	u8 triggers(u8 ev, u8 outs, bool qualifying);
	void store(const u8 rec[8], double t);
	void set_events(u8 ev);
	void src_w(int n, int l);                 // Am9513 SRC n+1 / GATE n+1 levels
	void gate_w(int n, int l);
	void pulse_src(int n) { src_w(n, 1); src_w(n, 0); }
	void log(const char *w, int r, u8 v) { if (trace) fprintf(stderr, "[%.7f TTA %s %02x %02x]\n", clock ? *clock : 0.0, w, 0x70 + r, v); }
};
#endif
