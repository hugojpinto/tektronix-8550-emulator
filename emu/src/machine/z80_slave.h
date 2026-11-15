// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// A Z80 that the 8301's Emulator Controller drives (machine/emulator_board.h). The base owns
// the processor and its bus as the Emulator Controller presents it: memory through the
// 8301's memory map and breakpoints, the RTPA and TTA watching, SVC decoding on OUT.
// Two Z80s use it: the Language Processor (here: part of the 8301 itself, control E7, runs
// DOS/50's tools) and the Z80 emulator processor board (machine/epz80.h).
#ifndef TEK_MACHINE_Z80_SLAVE_H
#define TEK_MACHINE_Z80_SLAVE_H
#include "machine/emulator_board.h"
#include <cstdlib>
extern "C" {
#include "cpu/z80/superzazu/z80.h"
}

class z80_slave : public emulator_board
{
public:
	explicit z80_slave(tek8301 &m) : emulator_board(m) {}
	z80 cpu{};
	void setup();                              // power-on state, bus callbacks
	bool rtpa_visible() const override { return true; }   // the RTPA watches either
	// RESET clears PC, I, R, the IFFs and the interrupt mode and leaves the other registers (Zilog;
	// the sibling 8002 emulator's Z80 DEBUG decode test relies on it); setup() is the power-on state
	void forced_reset() override;
	void step() override { z80_step(&cpu); }
	u16 pc() const override { return cpu.pc; }
	// a halted Z80 fetches at the HALT's own address (the sibling 8002 emulator's Z80 DEBUG decode
	// test expects PC next there); the core holds the address after it
	// Z80 machine cycles: an opcode fetch 4 states (with refresh), a memory cycle 3, an I/O cycle 4;
	// no wait states at 4 MHz on the 8550 (070-3964, "4 MHz and Below")
	void bus_states(int &f, int &m, int &io) const override { f = 4; m = 3; io = 4; }
	u16 fetch_address() const override { return cpu.halted ? u16(cpu.pc - 1) : cpu.pc; }
	bool halted() const override { return cpu.halted; }
	u64 cycles() const override { return cpu.cyc; }
protected:
	void jump(u16 target);
	// what differs between the two Z80s on the bus
	virtual bool prototype_io() const { return false; }   // I/O goes to the prototype (modes 1/2)
	virtual bool svc_stops() const { return false; }      // an SVC stops the processor until the next F9 command
private:
	static u8 mem_rd(void *u, u16 a);
	static void mem_wr(void *u, u16 a, u8 d);
	static u8 port_in(z80 *z, u16 port);       // full 16-bit address (A8-A15 from A or B)
	static void port_out(z80 *z, u16 port, u8 d);
};

// The Language Processor: the 8301's own Z80 (E7 D7 active, D2 slow clock)
class language_processor : public z80_slave
{
public:
	using z80_slave::z80_slave;
	const char *name() const override { return "LP"; }
	bool enabled() const override;
	double clock_hz() const override;
	bool tta_visible() const override { return false; }
	bool forced_jump(u16 target) override { jump(target); return false; }
	bool forced_interrupt() override { return false; }
};
#endif
