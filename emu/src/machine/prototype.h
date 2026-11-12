// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The "prototype" (user target system) side of an emulator processor's probe.
// In emulation modes 1 and 2 the emulator processor's memory accesses to
// blocks mapped U (user) and all of its I/O go to the target; see
// notes/prototype-link.md. The default implementation is "no target".
#ifndef TEK_PROTOTYPE_H
#define TEK_PROTOTYPE_H
#include "emu.h"

class prototype_bus
{
public:
	virtual ~prototype_bus() = default;
	virtual bool connected() const { return false; }
	virtual u8   mem_read(u16 a) { (void)a; return 0xff; }       // undriven bus
	virtual u8   mem_fetch(u16 a) { return mem_read(a); }          // opcode fetch (M1)
	virtual void mem_write(u16 a, u8 d) { (void)a; (void)d; }
	virtual u8   io_read(u16 port) { (void)port; return 0xff; }
	virtual void io_write(u16 port, u8 d) { (void)port; (void)d; }
	virtual bool irq() { return false; }                           // INT line (level)
	virtual bool nmi() { return false; }                           // NMI line (edge seen as level)
	virtual u8   irq_vector() { return 0xff; }                     // data bus during INTA (mode 0/2)
	virtual void reset() {}
	virtual bool reset_line() { return false; }                    // v3: target asserts RESET
	virtual unsigned take_wait() { return 0; }                     // v3: WAIT T-states since the last call
	virtual void poll(unsigned long z80_cycles) { (void)z80_cycles; }   // before each instruction; arg = T-states in 4 MHz units
	virtual double clock_hz() const { return 0; }                  // v4: the prototype's clock, which the probe feeds the processor (0: not known)
};
#endif
