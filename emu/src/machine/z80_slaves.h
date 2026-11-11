// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The Z80s the 8301's Emulator Controller drives (machine/emulator_board.h): the Language
// Processor (E7), which runs DOS/50's tools, and the Z80 emulator processor board (8300E01,
// control port E3), which has firmware (high level, tek8301::ep_firmware), an NMI and
// prototype pins. Each owns its processor; both reach memory and I/O through the 8301's
// Z80 bus callbacks (tek8301::lp_read ...), set up by tek8301::z80_setup.
#ifndef TEK_MACHINE_Z80_SLAVES_H
#define TEK_MACHINE_Z80_SLAVES_H
#include "machine/emulator_board.h"
extern "C" {
#include "cpu/z80/superzazu/z80.h"
}

class z80_slave : public emulator_board
{
public:
	explicit z80_slave(tek8301 &m) : emulator_board(m) {}
	z80 cpu{};
	bool rtpa_visible() const override { return true; }   // the RTPA watches either
	void forced_reset() override;
	void step() override { z80_step(&cpu); }
	u16 pc() const override { return cpu.pc; }
	bool halted() const override { return cpu.halted; }
	u64 cycles() const override { return cpu.cyc; }
protected:
	void jump(u16 target);
};

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

class z80_board : public z80_slave
{
public:
	using z80_slave::z80_slave;
	const char *name() const override { return "EP"; }
	bool enabled() const override;
	double clock_hz() const override { return 4e6; }
	const emulator_specifics *specifics() const override;
	bool tta_visible() const override { return true; }
	bool forced_jump(u16 target) override;
	bool forced_interrupt() override;
	void pre_step() override;
	void post_step() override;
};
#endif
