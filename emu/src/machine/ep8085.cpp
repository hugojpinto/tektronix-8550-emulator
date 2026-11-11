// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8085 emulator processor board -- see ep8085.h.
#include "machine/ep8085.h"
#include "machine/tek8301.h"
#include <cstdio>

ep8085_board::ep8085_board(tek8301 &m) : emulator_board(m)
{
	m_bus.b = this;
	m_spec = make_i8085_specifics(cpu);
}

// the bus: through the 8301's memory map and breakpoints; the TTA, in run mode, sees every
// cycle (an 8085 I/O cycle puts the port number on both halves of the address bus: OUT F7
// is acquired as 00F7F7, TTA users manual 1-10)
u8 ep8085_board::bus_t::read(u16 a)
{
	tek8301 &m = b->mdu;
	m.bp_check(a, false); u8 v = m.slave_rd(a, false);
	if (m.m_tta_watch) m.tta_board.target_bus(b->m_t, a, v, tta::MREAD);
	return v;
}
u8 ep8085_board::bus_t::fetch(u16 a)
{
	tek8301 &m = b->mdu;
	m.bp_check(a, false); u8 v = m.slave_rd(a, true);
	if (m.m_tta_watch) m.tta_board.target_bus(b->m_t, a, v, tta::FETCH);
	return v;
}
void ep8085_board::bus_t::write(u16 a, u8 d)
{
	tek8301 &m = b->mdu;
	m.bp_check(a, true);
	if (m.m_tta_watch) m.tta_board.target_bus(b->m_t, a, d, tta::MWRITE);
	m.slave_wr(a, d);
}
u8 ep8085_board::bus_t::io_read(u8 port)
{
	tek8301 &m = b->mdu;
	if (m.m_tta_watch) m.tta_board.target_bus(b->m_t, u16(port | (port << 8)), 0xff, tta::IOREAD);
	return 0xff;
}
void ep8085_board::bus_t::io_write(u8 port, u8 d)
{
	tek8301 &m = b->mdu;
	if (m.m_tta_watch) m.tta_board.target_bus(b->m_t, u16(port | (port << 8)), d, tta::IOWRITE);
	// SVC n = OUT (F8h-n) in the block selected by F3, as on the Z80 board
	if ((m.m_f9 & 0x10) && (port & 0xf8) == ((m.m_f3 & 0x0f) << 4)) {
		m.ec_interrupt(tek8301::INT_SVC1 + (7 - (port & 7)));
		m.m_svc_stop = true;
	}
}

void ep8085_board::io_write(u8, u8 d)
{
	if (mdu.ep_trace) fprintf(stderr, "[%.3f E5 <- %02x]\n", mdu.m_cycles / tek8301::CPU_HZ, d);
	control = d;
}

// firmware PROM (not dumped) in F800-FBFF while E5<7> = 1, E5<4> = 0
bool ep8085_board::prom_window(u16 a, u8 &v) const
{
	if (present && (control & 0x90) == 0x80 && (a & 0xfc00) == 0xf800) { v = 0xff; return true; }
	return false;
}

bool ep8085_board::forced_jump(u16 target)
{
	tek8301 &m = mdu;
	if (m.ep_trace) fprintf(stderr, "[%.3f forced jump %04x -> EP85]\n", m.m_cycles / tek8301::CPU_HZ, target);
	if (target >= 0xf800) {
		// a jump into the firmware together with a forced interrupt (DOS/50's break:
		// F9 = F9) is the firmware's TRAP-based entry: it saves the user's PC, which the
		// restore entry later returns to (DOS/50 does not force a jump after restoring
		// during TRACE). High level: remember that PC and consume the interrupt.
		if (m.m_fi_pending || (target & 0xff) == 0xed) m_resume = cpu.pc();
		m.m_fi_pending = false;
		cpu.set_pc(target);
		firmware(target);
		return true;
	}
	if (m.tta_board.target_watch()) { m.tta_board.run.hz = clock_hz(); m.tta_board.run_start(target != m.m_lp_pc_next, m_t); }
	cpu.set_pc(target);
	return false;
}

// forced interrupt: the 8085's TRAP (also wakes it from HLT). The line stays asserted
// until an instruction has run: MAME's core cancels a TRAP whose line drops first.
// Held until the processor can run.
bool ep8085_board::forced_interrupt()
{
	tek8301 &m = mdu;
	m_trap = false;
	if (m.m_fi_pending && m.cpu.halted() && m.m_lp_live && !m.m_ec_pend && !m.m_svc_stop) {
		m.m_fi_pending = false;
		cpu.set_input_line(I8085_TRAP_LINE, ASSERT_LINE);
		m_trap = true;
		if (cpu.halted()) {
			cpu.run(1);                             // TRAP taken at the next M1 time, then one instruction
			cpu.set_input_line(I8085_TRAP_LINE, CLEAR_LINE); m_trap = false;
			m.m_lp_pc_last = cpu.pc();
			if (m.ep_trace) fprintf(stderr, "[%.3f EP85 TRAP from HLT -> pc %04x halted %d]\n", m.m_cycles / tek8301::CPU_HZ, cpu.pc(), int(cpu.halted()));
			if (cpu.halted()) { m.ec_interrupt(tek8301::INT_SLV_HALT); return true; }
		}
	}
	return false;
}

void ep8085_board::step()
{
	if (mdu.ep_trace && m_steps++ < 40) fprintf(stderr, "[EP85 %04x]\n", cpu.pc());
	m_t += u64(cpu.run(1));                    // one instruction
	if (m_trap) { cpu.set_input_line(I8085_TRAP_LINE, CLEAR_LINE); m_trap = false; }
}

// Firmware (high level). Register block: 14 bytes at program memory 0000-000D (DOS/50 swaps
// it with its saved context around each call). Entries (forced jump, FB:FA active low): FBEA
// restore, FBED dump (DOS/50 V1.0 GO / break); FAEA / FAED are alternates DOS/50 picks by a
// flag: the IE bit of block byte 11 (set: FAEA, which ends with EI). Block layout (found by
// filling the dump with marker bytes and reading DS): 0 L 1 H 2 E 3 D 4 C 5 B 6 A 7 F
// 8 ? (never touched by DOS/50) 9-10 SP 11 RIM value (SID 80, I7.5 40, IE 08) 12 SIM masks
// (bits 2-0: M7.5 M6.5 M5.5, 1 = masked) 13 written by DOS/50, not the firmware: the E5
// status after each dump (SOD in bit 0); the PC comes from the PC latch. DOS/50's
// initial context has 11 = 07, 12 = 0F.
void ep8085_board::firmware(u16 entry)
{
	tek8301 &m = mdu;
	auto pm = [&](int o) { return m.m_progmem[o & 0xffff]; };
	if (m.ep_trace) {
		fprintf(stderr, "[%.3f EP85 firmware entry %04x, block:", m.m_cycles / tek8301::CPU_HZ, entry);
		for (int i = 0; i < 14; i++) fprintf(stderr, " %02x", pm(i));
		fprintf(stderr, "]\n");
	}
	// the firmware PROM is mirrored through F800-FBFF (8085 diag 06/00E1 calls the dump
	// entry as F8ED; DOS/50 uses FBEx or FAEx): decode the entry by its low byte
	u16 e = u16(0xfb00 | (entry & 0xff));
	if (e == 0xfbea) {                               // restore registers from the block
		cpu.set_hl(u16(pm(0) | pm(1) << 8)); cpu.set_de(u16(pm(2) | pm(3) << 8)); cpu.set_bc(u16(pm(4) | pm(5) << 8));
		cpu.set_af(u16(pm(7) | pm(6) << 8)); cpu.set_sp(u16(pm(9) | pm(10) << 8));
		cpu.set_im(u8((cpu.im() & 0xf0) | (pm(11) & 0x08) | (pm(12) & 0x07)));   // IE from the RIM byte, masks (SIM) from 12
		cpu.set_pc(m_resume);                            // resumes the user program (a forced jump may follow)
		m.ec_interrupt(tek8301::INT_SVC1 + 6, false);    // Debug SVC 1 (as on the Z80 board)
		m.m_svc_stop = true;
	} else if (e == 0xfbed) {                        // dump registers into the block (after a break)
		auto put = [&](int o, u8 v) { m.m_progmem[o] = v; };
		put(0, u8(cpu.hl())); put(1, u8(cpu.hl() >> 8)); put(2, u8(cpu.de())); put(3, u8(cpu.de() >> 8));
		put(4, u8(cpu.bc())); put(5, u8(cpu.bc() >> 8)); put(6, u8(cpu.af() >> 8)); put(7, u8(cpu.af()));
		put(9, u8(cpu.sp())); put(10, u8(cpu.sp() >> 8));
		put(11, u8((cpu.im() & 0x48) | (cpu.im() & 0x07)));     // RIM: I7.5 pending 40, IE 08, masks
		put(12, u8((pm(12) & 0xf8) | (cpu.im() & 0x07)));       // SIM masks (DS M7/M6/M5)
		m.ec_interrupt(tek8301::INT_SVC1 + 6, false);
		m.m_svc_stop = true;
	} else {
		if (m.ep_trace) fprintf(stderr, "[EP85 firmware entry %04x unknown: HLT]\n", entry);
		cpu.set_pc(entry);
	}
}
