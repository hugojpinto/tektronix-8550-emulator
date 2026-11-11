// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The Z80 slaves of the Emulator Controller -- see z80_slave.h.
#include "machine/z80_slave.h"
#include "machine/tek8301.h"
#include <cstdio>
#include <cstdlib>

static const bool slave_io_trace = getenv("TEK_SLAVEIO") != nullptr;   // trace LP/EP port I/O that goes nowhere

void z80_slave::setup()
{
	z80_init(&cpu);
	cpu.read_byte = &mem_rd;
	cpu.write_byte = &mem_wr;
	cpu.port_in = &port_in;
	cpu.port_out = &port_out;
	cpu.userdata = this;
}

// forced jump: FA/FB/F5 are loaded active-low (DOS/50 writes FB:FA = FEFF to start a
// program at 0100, and FF:07F9 for the board firmware at 00:F806)
void z80_slave::jump(u16 target)
{
	if (mdu.ep_trace) fprintf(stderr, "[%.3f forced jump %02x:%04x -> %s]\n", mdu.m_cycles / tek8301::CPU_HZ, u8(~mdu.m_f5), target, name());
	cpu.halted = false;
	cpu.pc = target;
}

// ---- the bus: the 8301's memory map and breakpoints; the RTPA and, in run mode, the TTA
// see every cycle (the TTA only watches a slave whose tta_visible() is true)
u8 z80_slave::mem_rd(void *u, u16 a)
{
	z80_slave &s = *static_cast<z80_slave *>(u);
	tek8301 &m = s.mdu;
	m.bp_check(a, false);
	u8 v = m.slave_rd(a, s.cpu.m1);
	if (m.m_rtpa_watch) m.rtpa_board.bus(a, v, s.cpu.m1, false, false);
	if (m.m_tta_watch) m.tta_board.target_cycle(s.cpu.cyc, s.clock_hz(), tta::cycle8(a, v, s.cpu.m1 ? tta::FETCH : tta::MREAD));
	return v;
}

void z80_slave::mem_wr(void *u, u16 a, u8 d)
{
	z80_slave &s = *static_cast<z80_slave *>(u);
	tek8301 &m = s.mdu;
	m.bp_check(a, true);
	if (m.m_rtpa_watch) m.rtpa_board.bus(a, d, false, true, false);
	if (m.m_tta_watch) m.tta_board.target_cycle(s.cpu.cyc, s.clock_hz(), tta::cycle8(a, d, tta::MWRITE));
	m.slave_wr(a, d);
}

u8 z80_slave::port_in(z80 *z, u16 port)
{
	z80_slave &s = *static_cast<z80_slave *>(z->userdata);
	tek8301 &m = s.mdu;
	const u8 v = s.prototype_io() ? m.target->io_read(port) : 0xff;
	if (m.m_tta_watch) m.tta_board.target_cycle(s.cpu.cyc, s.clock_hz(), tta::cycle8(port, v, tta::IOREAD));   // the TTA sees the I/O cycle
	else if (slave_io_trace && !s.prototype_io()) fprintf(stderr, "[%.3f %s in  %04x @%04x]\n", m.m_cycles / tek8301::CPU_HZ, s.name(), port, z->pc);
	return v;
}

void z80_slave::port_out(z80 *z, u16 port, u8 d)
{
	z80_slave &s = *static_cast<z80_slave *>(z->userdata);
	tek8301 &m = s.mdu;
	if (m.m_tta_watch) m.tta_board.target_cycle(s.cpu.cyc, s.clock_hz(), tta::cycle8(port, d, tta::IOWRITE));
	// SVC: OUT to the 8-port block selected by the SVC map (F3 D3-D0 = A7-A4, default F0-F7).
	// SVC n is OUT (F8h-n): F7 = SVC 1 ... F0 = SVC 8 (Z80 Emulator Specifics, Table 7C-3)
	// (SVC decode uses A0-A7 only; A8-A15 = A are ignored)
	if ((m.m_f9 & 0x10) && (port & 0xf8) == ((m.m_f3 & 0x0f) << 4)) {
		m.ec_interrupt(tek8301::INT_SVC1 + (7 - (port & 7)));
		// on the emulator board an SVC also stops the processor until the next F9 command
		// (Z80 diag 07/0070); the Language Processor resumes when the SVC is taken (DOS/50 tools)
		if (s.svc_stops()) m.m_svc_stop = true;
		return;
	}
	if (s.prototype_io()) { m.target->io_write(port, d); return; }   // modes 1/2: target I/O
	if (slave_io_trace) fprintf(stderr, "[%.3f %s out %04x <- %02x @%04x]\n", m.m_cycles / tek8301::CPU_HZ, s.name(), port, d, z->pc);
}

// ---- the Language Processor
bool language_processor::enabled() const { return (mdu.m_e7 & 0x80) != 0; }
double language_processor::clock_hz() const { return (mdu.m_e7 & 0x04) ? 2.5e6 : 4e6; }
