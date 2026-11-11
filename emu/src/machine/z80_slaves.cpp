// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The Z80 slaves of the Emulator Controller -- see z80_slaves.h.
#include "machine/z80_slaves.h"
#include "machine/tek8301.h"
#include <cstdio>

void z80_slave::forced_reset() { z80 keep = cpu; mdu.z80_setup(cpu); cpu.read_byte = keep.read_byte; }

// forced jump: FA/FB/F5 are loaded active-low (DOS/50 writes FB:FA = FEFF to start a
// program at 0100, and FF:07F9 for the board firmware at 00:F806)
void z80_slave::jump(u16 target)
{
	if (mdu.ep_trace) fprintf(stderr, "[%.3f forced jump %02x:%04x -> %s]\n", mdu.m_cycles / tek8301::CPU_HZ, u8(~mdu.m_f5), target, name());
	cpu.halted = false;
	cpu.pc = target;
}

bool language_processor::enabled() const { return (mdu.m_e7 & 0x80) != 0; }
double language_processor::clock_hz() const { return (mdu.m_e7 & 0x04) ? 2.5e6 : 4e6; }

bool z80_board::enabled() const { return mdu.ep_enabled(); }
const emulator_specifics *z80_board::specifics() const { return mdu.m_spec_z80.get(); }

bool z80_board::forced_jump(u16 target)
{
	tek8301 &m = mdu;
	// the TTA starts a run; GO with a new address (not the stop address) resets its counters
	if (target < 0xf800 && m.tta_board.target_watch()) { cpu.halted = false; m.tta_board.run_start(target != m.m_lp_pc_next, cpu.cyc); }
	jump(target);
	if (target >= 0xf800) { m.ep_firmware(target); return true; }   // board firmware space
	return false;
}

// forced interrupt: the emulator processor's NMI, which also wakes it from HALT
bool z80_board::forced_interrupt()
{
	tek8301 &m = mdu;
	if (m.m_fi_pending && m.cpu.halted() && m.m_lp_live && !m.m_ec_pend) {
		m.m_fi_pending = false;
		z80_gen_nmi(&cpu);
		if (cpu.halted) z80_step(&cpu);          // halted: the NMI is taken at the next M1 time
	}
	return false;
}

// modes 1/2: the prototype's lines
void z80_board::pre_step()
{
	tek8301 &m = mdu;
	if (!m.proto_mode()) return;
	m.target->poll(cpu.cyc);
	// v3: RESET from the target (reset button, power-on) resets the emulator
	// processor on its rising edge; this does not send 'Z' back to the target
	bool rs = m.target->reset_line();
	if (rs && !m.m_trst_prev) { fprintf(stderr, "[%.3f target RESET: emulator Z80 reset, PC was %04x]\n", m.m_cycles / tek8301::CPU_HZ, cpu.pc); z80 keep = cpu; m.z80_setup(cpu); cpu.read_byte = keep.read_byte; cpu.cyc = keep.cyc; }
	m.m_trst_prev = rs;
	bool n = m.target->nmi();
	if (n && !m.m_nmi_prev) z80_gen_nmi(&cpu);
	m.m_nmi_prev = n;
	if (m.target->irq() && cpu.iff1) z80_gen_int(&cpu, m.target->irq_vector());
}

void z80_board::post_step() { if (mdu.proto_mode()) cpu.cyc += mdu.target->take_wait(); }   // v3: target WAIT states
