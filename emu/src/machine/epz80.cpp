// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Z80 emulator processor board -- see epz80.h.
#include "machine/epz80.h"
#include "machine/tek8301.h"
#include <cstdio>

epz80_board::epz80_board(tek8301 &m) : z80_slave(m), m_spec(make_z80_specifics(cpu)) {}

// E3 read: 20 board present; 10 processor inactive (HALT, or held by an RTPA/TTA break)
u8 epz80_board::io_read(u8)
{
	const tek8301 &m = mdu;
	const u8 v = present ? u8(0x20 | ((control & 0x80) && (cpu.halted || m.rtpa_board.stopped() || m.tta_board.stopped()) ? 0x10 : 0x00)) : 0x00;
	if (m.ep_trace) fprintf(stderr, "[%.3f E3 read %02x]\n", m.m_cycles / tek8301::CPU_HZ, v);
	return v;
}

void epz80_board::io_write(u8, u8 d)
{
	if (mdu.ep_trace) fprintf(stderr, "[%.3f E3 <- %02x]\n", mdu.m_cycles / tek8301::CPU_HZ, d);
	control = d;
}

// Firmware PROM: 512 bytes at F800-F9FF of program memory while E3<7> = 1 and E3<1> = 0.
// Every firmware run clears bit 1 (DOS/50 80/C0, Z80 diag E0/E1 94) and every user run sets
// it (92/B2/F2, 96, 86), so bit 1 maps the PROM for the Z80; that the 2650's view (diag
// D0 reads it with E3 = 80) uses the same bit is inferred. Not dumped: reads FF unless
// -ep-prom supplies an image (Z80 diag 07/00D0 checksums it, expecting 0852).
bool epz80_board::firmware_mapped() const { return present && (control & 0x82) == 0x80; }

bool epz80_board::prom_window(u16 a, u8 &v) const
{
	if (!firmware_mapped() || (a & 0xfe00) != 0xf800) return false;
	v = prom.empty() ? 0xff : prom[(a - 0xf800) % prom.size()];
	return true;
}

bool epz80_board::forced_jump(u16 target)
{
	tek8301 &m = mdu;
	const bool fw = firmware_mapped() && (target & 0xfe00) == 0xf800;   // into the PROM
	// the user's program starts (GO, or a step of TRACE); a new address (not where it
	// stopped) is a new run for the TTA software
	if (!fw) m.user_go(m.new_go_address(target));
	jump(target);
	if (fw) {
		// DOS/50 dumps a Z80 stopped by HALT with F9 = F9: the forced interrupt (NMI) wakes
		// it so that it takes the jump, and block byte 26 = 02 tells the firmware so (DOS/50
		// 6DF3-6E07). High level: the firmware absorbs that NMI.
		m.m_fi_pending = false;
		firmware(target);
		return true;
	}
	return false;
}

// forced interrupt: the emulator processor's NMI, which also wakes it from HALT
bool epz80_board::forced_interrupt()
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
void epz80_board::pre_step()
{
	tek8301 &m = mdu;
	if (!prototype_mode()) return;
	m.target->poll(cpu.cyc);
	// v3: RESET from the target (reset button, power-on) resets the emulator
	// processor on its rising edge; this does not send 'Z' back to the target
	bool rs = m.target->reset_line();
	if (rs && !m_trst_prev) { fprintf(stderr, "[%.3f target RESET: emulator Z80 reset, PC was %04x]\n", m.m_cycles / tek8301::CPU_HZ, cpu.pc); const u64 c = cpu.cyc; setup(); cpu.cyc = c; }
	m_trst_prev = rs;
	bool n = m.target->nmi();
	if (n && !m_nmi_prev) z80_gen_nmi(&cpu);
	m_nmi_prev = n;
	if (m.target->irq() && cpu.iff1) z80_gen_int(&cpu, m.target->irq_vector());
}

void epz80_board::post_step() { if (prototype_mode()) cpu.cyc += mdu.target->take_wait(); }   // v3: target WAIT states

// Firmware (not dumped): high-level emulation of what DOS/50 expects. DOS/50 lends program
// memory 0000-001A to the board as a register block
//   0 IM  1 IFF1<7>/IFF2<6>  2 R  3 I  4..11 L' H' E' D' C' B' A' F'
//   12-13 IY  14-15 IX  16..23 L H E D C B A F  24-25 SP
//   26 written by DOS/50 before a dump: 02 when the processor stopped at a HALT (and the
//      dump entry is then forced together with an NMI), else 00; not read back
// and forces a jump to the entry (FB:FA/F5 written active low) with E3<7> set:
//   F800 PUT byte to prototype memory   (4-byte block: 0 data, 1-2 address high first,
//        3 status: firmware writes 0 after a good read-back -- DOS/50 MOV checks it)
//   F803 GET byte from prototype memory (result in block 0, status in block 3)
//   F806 restore registers, F809 dump registers. The firmware restores the registers
// and must answer with an SVC (DOS/50 7138 waits for any SVC, error 45 on HALT).
void epz80_board::firmware(u16 entry)
{
	tek8301 &m = mdu;
	auto pm = [&](int o) { return m.m_progmem[o & 0xffff]; };
	auto put = [&](int o, u8 v) { m.m_progmem[o] = v; };
	z80 &ep = cpu;
	if (m.ep_trace) {
		fprintf(stderr, "[%.3f EP firmware entry %04x, block:", m.m_cycles / tek8301::CPU_HZ, entry);
		for (int i = 0; i < 27; i++) fprintf(stderr, " %02x", pm(i));
		fprintf(stderr, "]\n");
	}
	if (entry == 0xf806) {                       // restore
		ep.interrupt_mode = pm(0) & 3;
		ep.iff1 = pm(1) & 0x80; ep.iff2 = pm(1) & 0x40;
		ep.r = pm(2); ep.i = pm(3);
		ep.l_ = pm(4); ep.h_ = pm(5); ep.e_ = pm(6); ep.d_ = pm(7); ep.c_ = pm(8); ep.b_ = pm(9); ep.a_ = pm(10); ep.f_ = pm(11);
		ep.iy = u16(pm(12) | (pm(13) << 8)); ep.ix = u16(pm(14) | (pm(15) << 8));
		ep.l = pm(16); ep.h = pm(17); ep.e = pm(18); ep.d = pm(19); ep.c = pm(20); ep.b = pm(21); ep.a = pm(22);
		u8 f = pm(23);
		ep.sf = f & 0x80; ep.zf = f & 0x40; ep.yf = f & 0x20; ep.hf = f & 0x10; ep.xf = f & 0x08; ep.pf = f & 0x04; ep.nf = f & 0x02; ep.cf = f & 0x01;
		ep.sp = u16(pm(24) | (pm(25) << 8));
		ep.halted = false;
		m.ec_interrupt(tek8301::INT_SVC1 + 6, false);   // Debug SVC 1 (dump/restore) = SVC 7, int 22 (8301 Table 7-x; Z80 diag 07/00E1)
	} else if (entry == 0xf809) {                // dump (after a break)
		put(0, ep.interrupt_mode); put(1, u8((ep.iff1 ? 0x80 : 0) | (ep.iff2 ? 0x40 : 0)));
		put(2, ep.r); put(3, ep.i);
		put(4, ep.l_); put(5, ep.h_); put(6, ep.e_); put(7, ep.d_); put(8, ep.c_); put(9, ep.b_); put(10, ep.a_); put(11, ep.f_);
		put(12, u8(ep.iy)); put(13, u8(ep.iy >> 8)); put(14, u8(ep.ix)); put(15, u8(ep.ix >> 8));
		put(16, ep.l); put(17, ep.h); put(18, ep.e); put(19, ep.d); put(20, ep.c); put(21, ep.b); put(22, ep.a);
		put(23, u8((ep.sf ? 0x80 : 0) | (ep.zf ? 0x40 : 0) | (ep.yf ? 0x20 : 0) | (ep.hf ? 0x10 : 0) |
		           (ep.xf ? 0x08 : 0) | (ep.pf ? 0x04 : 0) | (ep.nf ? 0x02 : 0) | (ep.cf ? 0x01 : 0)));
		put(24, u8(ep.sp)); put(25, u8(ep.sp >> 8));
		m.ec_interrupt(tek8301::INT_SVC1 + 6, false);   // Debug SVC 1 (dump/restore) = SVC 7
	} else if (entry == 0xf800) {                // PUT: write block[0] at block[1]:block[2], verify -> block[3] = 0 ok
		u16 a = u16((pm(1) << 8) | pm(2));
		m.target->mem_write(a, pm(0));
		put(3, (m.target->mem_read(a) == pm(0)) ? 0x00 : 0x01);   // DOS/50 MOV: nonzero = error 3C
		m.ec_interrupt(tek8301::INT_SVC1 + 7, false);   // Debug SVC 2 (get/put) = SVC 8, int 23
	} else if (entry == 0xf803) {                // GET: read prototype memory at block[1]:block[2] into block[0]
		put(0, m.target->mem_read(u16((pm(1) << 8) | pm(2))));
		put(3, 0x00);
		m.ec_interrupt(tek8301::INT_SVC1 + 7, false);   // Debug SVC 2 (get/put) = SVC 8
	} else if (m.ep_trace) fprintf(stderr, "[EP firmware entry %04x unknown]\n", entry);
	if (entry == 0xf800 || entry == 0xf803 || entry == 0xf806 || entry == 0xf809)
		m.m_svc_stop = true;                     // the firmware's SVC OUT stops the board like any SVC
	if (m.ep_trace) {
		fprintf(stderr, "[   block now:");
		for (int i = 0; i < 27; i++) fprintf(stderr, " %02x", pm(i));
		fprintf(stderr, "]\n");
	}
}
