// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// A processor the 8301's Emulator Controller runs as its "slave" while the 2650 is halted:
// an emulator processor board (Z80 8300E04, 8085 8300E06, ...) or the Language Processor.
// (The 8086/8088 emulator is not one: it has its own kernel and ports, machine/ep8086.h.)
// The Emulator Controller's own behaviour -- forced reset, jump and interrupt latched by
// F9, breakpoints, SVCs, single cycle, halt detection, and the RTPA/TTA watching the bus --
// is the same for all of them and lives once, in tek8301::slave_run. A slave supplies what
// differs: its processor, the meaning of each forced action, its firmware (where the board
// has one), its control port and the window its firmware PROM opens to the 2650.
#ifndef TEK_MACHINE_EMULATOR_BOARD_H
#define TEK_MACHINE_EMULATOR_BOARD_H
#include "emu.h"

class tek8301;
class emulator_specifics;

class emulator_board
{
public:
	explicit emulator_board(tek8301 &m) : mdu(m) {}
	virtual ~emulator_board() = default;
	virtual const char *name() const = 0;
	virtual bool enabled() const = 0;          // fitted and enabled by its control register
	// the processor's clock, and the board's Emulator Specifics (none for the Language Processor)
	virtual double clock_hz() const = 0;
	virtual const emulator_specifics *specifics() const { return nullptr; }
	// the RTPA and the TTA watch this processor's bus
	virtual bool rtpa_visible() const = 0;
	virtual bool tta_visible() const = 0;

	// ---- the board's own control port(s), as the 2650 sees them
	virtual bool decodes(u8 port) const { (void)port; return false; }
	virtual u8 io_read(u8 port) { (void)port; return 0x00; }
	virtual void io_write(u8 port, u8 d) { (void)port; (void)d; }
	virtual void restart() {}                  // system reset
	// ---- what the Emulator Controller's F9 commands do to it (taken when the slave runs)
	virtual void forced_reset() = 0;
	virtual bool forced_jump(u16 target) = 0;  // true: handled in firmware, nothing to run yet
	virtual bool forced_interrupt() = 0;       // true: stop here for now
	// ---- running
	virtual void pre_step() {}                 // before each instruction (prototype lines ...)
	virtual void step() = 0;                   // one instruction
	virtual void post_step() {}                // after it, after the analysers (prototype WAIT ...)
	virtual u16 pc() const = 0;
	virtual bool halted() const = 0;
	virtual u64 cycles() const = 0;            // its own clock cycles so far
	// the firmware PROM as the 2650 sees it in program memory while the board is in firmware
	// mode; false where the board has none or it is not mapped
	virtual bool prom_window(u16, u8 &) const { return false; }
	// the prototype's memory and I/O (emulation modes 1/2): the Emulator Controller's memory
	// map sends user blocks there (mode 1), or all memory (mode 2)
	virtual bool prototype_mode() const { return false; }
	virtual bool prototype_all() const { return false; }
protected:
	tek8301 &mdu;
};
#endif
