// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix 8301 MDU -- see tek8301.h and notes/8301.md
#include "machine/tek8301.h"
#include <cstdio>
#include <cstdlib>

namespace {
bool read_file(const std::string &p, std::vector<u8> &v)
{
	FILE *f = fopen(p.c_str(), "rb"); if (!f) return false;
	int c; v.clear(); while ((c = fgetc(f)) != EOF) v.push_back(u8(c)); fclose(f); return true;
}
enum {
	INT_PARITY = 1, INT_WP = 2, INT_HSIN = 4, INT_HSOUT = 5, INT_REMOTE = 6, INT_AUX = 7,
	INT_TERM = 8, INT_TIMER = 9, INT_DMA = 10, INT_MTP = 11, INT_REFRESH = 15
};
// parity (int 1) is NOT edge: it stays until I/O port D2 is read, and so does the address latch
constexpr u32 EDGE_IRQS = (1u << INT_TIMER) | (1u << INT_DMA) | (1u << INT_REFRESH) | (1u << INT_WP) | 0xffff0000u;
}

tek8301::tek8301()
	: cpu(*this), m_sysram(0x10000, 0), m_progmem(0x10000, 0), m_par(0x10000, 0)
{
	lp_reset();
	prom.present = false;      // option; -prom on fits it
	rtpa_board.clock = &m_seconds;
	tta_board.clock = &m_seconds;
	// -prom fix, part 2: the later DOS/50's 'post event' (0304 there) delivers the target
	// task id as the woken task's r1; V1.0's 02FA delivers the caller's r1 instead. The
	// PROM framework (6002) stores that r1 as its own task id (60E7), so calls coming from
	// the relocated PROM driver get r1 = r0.
	// TEK_PCTRACE=lo-hi[@seconds]: log every 2650 instruction in lo..hi (hex), after the time given
	static unsigned pct_lo = 1, pct_hi = 0; static double pct_t = 0;
	if (const char *e = getenv("TEK_PCTRACE")) sscanf(e, "%x-%x@%lf", &pct_lo, &pct_hi, &pct_t);
	cpu.exec_hook = [this](u16 pc) {
		if (pc >= pct_lo && pc <= pct_hi && m_cycles / CPU_HZ >= pct_t)
			fprintf(stderr, "[%.4f 2650 %04x r0-3=%02x %02x %02x %02x]\n", m_cycles / CPU_HZ, pc, cpu.reg(0), cpu.reg(1), cpu.reg(2), cpu.reg(3));
		if (prom_dos50_fix && pc == 0x02fa) {
			u16 ret = cpu.ras_top();
			if (ret >= 0x6000 && ret < 0x7000) cpu.set_reg_bank(1, cpu.reg(0));
		}
	};
}

// --------------------------------------------------------------------------
// Language Processor (Z80A, slave) + Emulator Controller

tek8301::~tek8301() = default;

emulator_board *tek8301::active_slave() const
{
	if (m_ep85.enabled()) return const_cast<ep8085_board *>(&m_ep85);
	if (m_slave_ep.enabled()) return const_cast<z80_board *>(&m_slave_ep);
	if (m_slave_lp.enabled()) return const_cast<language_processor *>(&m_slave_lp);
	return nullptr;
}

const emulator_specifics *tek8301::emulator() const { const emulator_board *b = active_slave(); return b ? b->specifics() : nullptr; }
bool tek8301::slave_halted() const { const emulator_board *b = active_slave(); return b ? b->halted() : lp.halted; }

void tek8301::z80_setup(z80 &z)
{
	z80_init(&z);
	z.read_byte = &tek8301::lp_read;
	z.write_byte = &tek8301::lp_write;
	z.port_in = &tek8301::lp_in;
	z.port_out = &tek8301::lp_out;
	z.userdata = this;
}

void tek8301::lp_reset()
{
	z80_setup(lp);
	z80_setup(ep);
}

// Emulator processor firmware (not dumped): high-level emulation of what DOS/50
// expects. DOS/50 lends program memory 0000-001A to the board as a register block
//   0 IM  1 IFF1<7>/IFF2<6>  2 R  3 I  4..11 L' H' E' D' C' B' A' F'
//   12-13 IY  14-15 IX  16..23 L H E D C B A F  24-25 SP  26 ?
// and forces a jump to the entry (FB:FA/F5 written active low) with E3<7> set:
//   F800 PUT byte to prototype memory   (4-byte block: 0 data, 1-2 address high first,
//        3 status: firmware writes 0 after a good read-back -- DOS/50 MOV checks it)
//   F803 GET byte from prototype memory (result in block 0, status in block 3)
//   F806 restore registers, F809 dump registers. the firmware restores the registers
// and must answer with an SVC (DOS/50 7138 waits for any SVC, error 45 on HALT).
void tek8301::ep_firmware(u16 entry)
{
	auto pm = [&](int o) { return m_progmem[o & 0xffff]; };
	if (ep_trace) {
		fprintf(stderr, "[%.3f EP firmware entry %04x, block:", m_cycles / CPU_HZ, entry);
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
		ec_interrupt(INT_SVC1 + 6, false);       // Debug SVC 1 (dump/restore) = SVC 7, int 22 (8301 Table 7-x; Z80 diag 07/00E1)
	} else if (entry == 0xf809) {                // dump (after a break)
		auto put = [&](int o, u8 v) { m_progmem[o] = v; };
		put(0, ep.interrupt_mode); put(1, u8((ep.iff1 ? 0x80 : 0) | (ep.iff2 ? 0x40 : 0)));
		put(2, ep.r); put(3, ep.i);
		put(4, ep.l_); put(5, ep.h_); put(6, ep.e_); put(7, ep.d_); put(8, ep.c_); put(9, ep.b_); put(10, ep.a_); put(11, ep.f_);
		put(12, u8(ep.iy)); put(13, u8(ep.iy >> 8)); put(14, u8(ep.ix)); put(15, u8(ep.ix >> 8));
		put(16, ep.l); put(17, ep.h); put(18, ep.e); put(19, ep.d); put(20, ep.c); put(21, ep.b); put(22, ep.a);
		put(23, u8((ep.sf ? 0x80 : 0) | (ep.zf ? 0x40 : 0) | (ep.yf ? 0x20 : 0) | (ep.hf ? 0x10 : 0) |
		           (ep.xf ? 0x08 : 0) | (ep.pf ? 0x04 : 0) | (ep.nf ? 0x02 : 0) | (ep.cf ? 0x01 : 0)));
		put(24, u8(ep.sp)); put(25, u8(ep.sp >> 8));
		ec_interrupt(INT_SVC1 + 6, false);       // Debug SVC 1 (dump/restore) = SVC 7
	} else if (entry == 0xf800) {                // PUT: write block[0] at block[1]:block[2], verify -> block[3] = 0 ok
		u16 a = u16((pm(1) << 8) | pm(2));
		target->mem_write(a, pm(0));
		m_progmem[3] = (target->mem_read(a) == pm(0)) ? 0x00 : 0x01;   // DOS/50 MOV: nonzero = error 3C
		ec_interrupt(INT_SVC1 + 7, false);       // Debug SVC 2 (get/put) = SVC 8, int 23
	} else if (entry == 0xf803) {                // GET: read prototype memory at block[1]:block[2] into block[0]
		m_progmem[0] = target->mem_read(u16((pm(1) << 8) | pm(2)));
		m_progmem[3] = 0x00;
		ec_interrupt(INT_SVC1 + 7, false);       // Debug SVC 2 (get/put) = SVC 8
	} else if (ep_trace) fprintf(stderr, "[EP firmware entry %04x unknown]\n", entry);
	if (entry == 0xf800 || entry == 0xf803 || entry == 0xf806 || entry == 0xf809)
		m_svc_stop = true;                       // the firmware's SVC OUT stops the board like any SVC
	if (ep_trace) {
		fprintf(stderr, "[   block now:");
		for (int i = 0; i < 27; i++) fprintf(stderr, " %02x", pm(i));
		fprintf(stderr, "]\n");
	}
}

static const bool rem_trace = getenv("TEK_REMTRACE") != nullptr;
static const bool slave_io_trace = getenv("TEK_SLAVEIO") != nullptr;   // trace LP/EP port I/O that goes nowhere

u8 tek8301::lp_in(z80 *z, u16 port)
{
	tek8301 *m = static_cast<tek8301 *>(z->userdata);
	if (m->m_tta_watch) {   // the TTA sees the I/O cycle; the data is what the port answers
		u8 v = (z == &m->ep && m->proto_mode()) ? m->target->io_read(port) : 0xff;
		m->tta_board.target_bus(m->ep.cyc, port, v, tta::IOREAD);
		return v;
	}
	if (z == &m->ep && m->proto_mode()) return m->target->io_read(port);
	if (slave_io_trace) fprintf(stderr, "[%.3f %s in  %04x @%04x]\n", m->m_cycles / CPU_HZ, z == &m->ep ? "EP" : "LP", port, z->pc);
	return 0xff;
}

void tek8301::lp_out(z80 *z, u16 port, u8 d)
{
	tek8301 *m = static_cast<tek8301 *>(z->userdata);
	if (m->m_tta_watch) m->tta_board.target_bus(m->ep.cyc, port, d, tta::IOWRITE);
	// SVC: OUT to the 8-port block selected by the SVC map (F3 D3-D0 = A7-A4, default F0-F7).
	// SVC n is OUT (F8h-n): F7 = SVC 1 ... F0 = SVC 8 (Z80 Emulator Specifics, Table 7C-3)
	// (SVC decode uses A0-A7 only; A8-A15 = A are ignored)
	if ((m->m_f9 & 0x10) && (port & 0xf8) == ((m->m_f3 & 0x0f) << 4)) {
		m->ec_interrupt(INT_SVC1 + (7 - (port & 7)));
		// on the emulator board an SVC also stops the processor until the next F9 command
		// (Z80 diag 07/0070); the Language Processor resumes when the SVC is taken (DOS/50 tools)
		if (z == &m->ep) m->m_svc_stop = true;
		return;
	}
	if (slave_io_trace && !(z == &m->ep && m->proto_mode())) fprintf(stderr, "[%.3f %s out %04x <- %02x @%04x]\n", m->m_cycles / CPU_HZ, z == &m->ep ? "EP" : "LP", port, d, z->pc);
	if (z == &m->ep && m->proto_mode()) m->target->io_write(port, d);   // modes 1/2: target I/O
}

void tek8301::bp_check(u16 a, bool write)
{
	// F8: D0 BP1 on read, D1 BP1 on write, D2 BP2 on read, D3 BP2 on write (0 = enable)
	for (int i = 0; i < 2; i++)
		if (a == m_bp[i] && !(m_f8 & (1 << (2 * i + (write ? 1 : 0)))))
			m_bp_hit |= u8(1 << i);
}

void tek8301::ec_interrupt(int n, bool latch_pc)
{
	// F2 pending register bits (read active low): 7 BP1 6 BP2 5 S/CY 4 HALT 3 DIAG 2 RTPA
	switch (n) {
	case INT_BP1: m_ec_pend |= 0x80; break;
	case INT_BP2: m_ec_pend |= 0x40; break;
	case INT_SCY: m_ec_pend |= 0x20; break;
	case INT_SLV_HALT: m_ec_pend |= 0x10; break;
	case INT_DIAG: m_ec_pend |= 0x08; break;
	case INT_RTPA: m_ec_pend |= 0x04; break;   // DBG INT 29 from the RTPA (DOS/50 GO handler 681E tests F2 bit 2)
	default: break;
	}
	if (latch_pc) { const emulator_board *b = active_slave(); m_lp_pc_next = b ? b->pc() : lp.pc; }
	if (m_f9 & 0x08)                    // IM = 1: debug interrupts not masked
		m_pending |= 1u << n;
	update_irq();
}

// TTA interrupt: INT 29 like the RTPA's, but F2 bit 2 shows the TTA's own request
// flip-flop (cleared by a write to TTA port 72), not an Emulator Controller latch
void tek8301::tta_interrupt()
{
	if (m_f9 & 0x08) m_pending |= 1u << INT_RTPA;
	update_irq();
}

void tek8301::lp_run(int cycles2650)
{
	// Master halted but no slave able to run: the Emulator Controller reports the
	// slave as halted at once ("Processor Inactive Test", 070-2976 §4).
	// (Only when no slave is enabled at all -- not when an enabled slave is merely
	// blocked by an outstanding SVC/debug interrupt.)
	// Each new HALT of the master raises it again, even with the last one still pending
	// (TTA.TST module 50 halts three times without an F9 write in between).
	bool new_halt = cpu.halted() && !m_master_was_halted;
	m_master_was_halted = cpu.halted();
	// the 8086 emulator with emulation enabled (MASTER<3>) takes the halted 2650's bus itself
	// and reports its stop (ep8086_board::enter_kernel)
	if (m_ep86.run_enabled()) return;
	if (cpu.halted() && !slave_enabled() && (!(m_ec_pend & 0x10) || new_halt)) { ec_interrupt(INT_SLV_HALT); return; }
	if (!slave_enabled() || !cpu.halted()) return;
	slave_run(*active_slave(), cycles2650);
}

// The Emulator Controller driving a slave (070-2976 §7; notes/8301.md): forced reset, jump
// and interrupt latched by F9 are taken when the slave runs; then instructions run in step
// with the 2650's time until an event hands control back to the system processor: an SVC
// or write-protect violation (immediate), a breakpoint, single cycle or halt (at the next
// fetch, after PC-next is latched; priority SVC > BP1 > BP2 > S/CY > HALT), or an RTPA/TTA
// break (the board holds the processor).
void tek8301::slave_run(emulator_board &b, int cycles2650)
{
	if (m_fr_pending) { m_fr_pending = false; b.forced_reset(); m_ec_pend = 0; }
	if (m_fj_pending) {
		m_fj_pending = false;
		if (b.forced_jump(u16(~(m_fa | (m_fb << 8))))) return;   // FB:FA active low
	}
	if (b.forced_interrupt()) return;
	if (!lp_running()) return;
	m_lp_frac += cycles2650 * (b.clock_hz() / CPU_HZ);
	while (m_lp_frac > 0 && lp_running()) {
		const u64 c0 = b.cycles();
		m_lp_pc_last = b.pc();
		if (lp_trace_hook) lp_trace_hook(m_lp_pc_last);
		u32 before = m_pending;
		m_bp_hit = 0;
		b.pre_step();
		const bool watch = rtpa_board.present && b.rtpa_visible();
		const bool twatch = b.tta_visible() && tta_board.target_watch();   // the TTA in run mode: the program under test
		if (watch) { m_seconds = m_cycles / CPU_HZ; rtpa_board.instr_begin(b.pc()); m_rtpa_watch = true; }
		if (twatch) { tta_board.run.hz = b.clock_hz(); tta_board.instr_begin(b.cycles(), b.pc()); m_tta_watch = true; }
		b.step();
		if (watch) {
			m_rtpa_watch = false;
			rtpa_board.instr_end(b.pc(), int(b.cycles() - c0), b.clock_hz());
			if (rtpa_board.take_irq()) ec_interrupt(INT_RTPA);   // latches PC-next like a breakpoint (DS resumes there)
		}
		if (twatch) {
			m_tta_watch = false;
			tta_board.instr_end(b.pc(), b.cycles());
			if (tta_board.take_break()) ec_interrupt(INT_RTPA);   // a TTA break: INT 29, PC-next latched
			if (tta_board.take_irq()) tta_interrupt();
		}
		b.post_step();
		m_lp_frac -= double(b.cycles() - c0);
		if (rtpa_board.stopped() || tta_board.stopped()) break;   // RTPA/TTA break (INT 29): the board holds the processor
		if (m_pending != before) {              // SVC / write-protect violation (immediate): master takes over
			// a breakpoint matched by the same instruction is pending as well (diag 07/0070-0075)
			if (m_bp_hit & 1) ec_interrupt(INT_BP1);
			if (m_bp_hit & 2) ec_interrupt(INT_BP2);
			break;
		}
		if (m_bp_hit & 1) { ec_interrupt(INT_BP1); break; }
		if (m_bp_hit & 2) { ec_interrupt(INT_BP2); break; }
		if (!(m_f8 & 0x10)) { ec_interrupt(INT_SCY); break; }
		if (b.halted()) { ec_interrupt(INT_SLV_HALT); break; }
	}
	if (m_lp_frac > 0) m_lp_frac = 0;
}

bool tek8301::load_roms(const std::string &dir, std::string &err)
{
	if (!read_file(dir + "/160-0728-01.u5040", m_boot) || m_boot.size() < 2048 ||
		!read_file(dir + "/160-0802-01.u5030", m_diag) || m_diag.size() < 4096) {
		err = "missing 8301 ROMs (160-0728-01.u5040 boot / 160-0802-01.u5030 diag) in " + dir;
		return false;
	}
	return true;
}

void tek8301::power_on()
{
	std::fill(m_sysram.begin(), m_sysram.end(), 0);
	std::fill(m_par.begin(), m_par.end(), 0);
	m_d2 = 0; m_d3 = 0;
	restart();
}

void tek8301::restart()
{
	m_ea = 0; m_ee = 0; m_e9 = 0; m_ec = 0; m_led = 0; m_e7 = 0;
	m_pending = 0;
	prom.reset();
	remote.write(0, 3); aux.write(0, 3); term.write(0, 3);
	m_dma.reset();
	m_hsi_da = false; m_hsi_oe = false; m_hsi_tbre = true; m_hsi_tx_timer = 0;
	m_ec_pend = 0; m_f3 = 0x0f; m_f8 = 0x1f; m_f9 = 0x1f; m_lp_live = false; m_f4 = 0xff; m_e3 = 0; m_ep85.control = 0; m_fr_pending = m_fj_pending = m_fi_pending = false; m_svc_stop = false; m_f5 = 0xff;
	lp_reset();
	m_ep86.restart();
	cpu.reset();
}

// --------------------------------------------------------------------------
// memory

tek8301::busaddr tek8301::cpu_to_bus(offs_t a, bool)
{
	a &= 0x7fff;
	if (a < 0x2000) return { a, true };
	if (a < 0x4000) return { u32(((m_d3 >> 5) & 7) * 0x2000 + (a & 0x1fff)), true };
	u32 ba = u32((m_ee & 3) << 14) | (a & 0x3fff);
	return { ba, bool(m_ee & 4) };
}

u8 tek8301::mem_rd(busaddr b, bool ovl)
{
	const u8 ext = b.dma ? PROG_BANK : m_f4;   // 16-bit bus masters (DMA) don't use F4
	if (b.sys) {
		u16 a = u16(b.a);
		if (!b.dma && tta_board.ram_decodes(a)) { tta_board.ram_access(now_cycles(), a); if (tta_board.take_irq()) tta_interrupt(); return tta_board.ram_read(a); }   // TTA RAM inhibits system RAM
		if (!b.dma && (m_ee & 0x08) && a >= 0xfe00)                        // memory map / WP RAM
			return u8(m_map[((a & 0xff) << 1) | ((a >> 8) & 1)] | 0xfa);   // only D0/D2 exist
		// System RAM parity: a byte written under one complement setting and read
		// under the other fails the check (used by the power-up parity test).
		if (ovl && (m_d2 & 2) && m_par[a] != ((m_d2 >> 2) & 1)) {
			if (!(m_pending & (1u << INT_PARITY))) m_par_addr = a;
			m_pending |= 1u << INT_PARITY;
			update_irq();
		}
		return m_sysram[a];
	}
	return prog_rd(ext, u16(b.a));
}

void tek8301::mem_wr(busaddr b, u8 d)
{
	if (b.sys && !b.dma && (m_ee & 0x08) && (b.a & 0xffff) >= 0xfe00) {
		m_map[((b.a & 0xff) << 1) | ((b.a >> 8) & 1)] = d & 0x05;
		return;
	}
	if (b.sys && !b.dma && tta_board.ram_decodes(u16(b.a))) { tta_board.ram_access(now_cycles(), u16(b.a)); if (tta_board.take_irq()) tta_interrupt(); tta_board.ram_write(u16(b.a), d); return; }
	if (b.sys) { m_sysram[b.a & 0xffff] = d; m_par[b.a & 0xffff] = (m_d2 >> 2) & 1; }
	else prog_wr(b.dma ? PROG_BANK : m_f4, u16(b.a), d);
}

u8 tek8301::read(offs_t a)
{
	a &= 0x7fff;
	if (a < 0x1000 && (m_ea & 0x40)) return m_diag[a];
	if (a < 0x0800 && !(m_ea & 0x20)) return m_boot[a];
	return mem_rd(cpu_to_bus(a, false));
}

void tek8301::write(offs_t a, u8 d)
{
	mem_wr(cpu_to_bus(a & 0x7fff, true), d);   // ROM overlay is read-only; writes reach RAM
}

// --------------------------------------------------------------------------
// I/O

u8 tek8301::io_read(offs_t port)
{
	u8 v = 0xff;
	if (port >= 0x90 && port <= 0x9f) v = m_dma.read(port & 0x0f);
	else if (port >= 0xc0 && port <= 0xc7 && rtpa_board.present) { m_seconds = m_cycles / CPU_HZ; v = rtpa_board.read(port & 7); }
	// DOS/50 V1.0 predates the TTA: its GO handler attributes an INT 29 break only through the
	// RTPA status register (C2: 10 present, 08 stopped, 01/02 TRIG1/TRIG2). With no RTPA fitted,
	// the emulator's TTA command software (system/tta_console, standing in for the later DOS/50)
	// shows the TTA's break there, so DOS/50 prints <BREAK TRIGn>. An emulator bridge, not hardware.
	else if (port == 0xc2 && tta_board.present && tta_board.break_status()) v = tta_board.break_status();
	else if (port >= 0x70 && port <= 0x77 && tta_board.present) { m_seconds = double(now_cycles()) / CPU_HZ; v = tta_board.read(port & 7, now_cycles()); }
	else if (m_ep86.decodes(u8(port))) v = m_ep86.io_read(u8(port));
	else switch (port) {
	case 0xd0: v = prom.present ? prom.read(0) : 0x00; break;
	case 0xd1: v = prom.present ? prom.read(1) : 0x00; break;
	case 0xb8: v = m_b8 & 0x5f; break;
	case 0xb9: v = m_b9 & 0x5f; break;
	case 0xca: case 0xcb: v = remote.read(port & 1); if (rem_trace && (port & 1)) fprintf(stderr, "[%.3f REM rx %02x '%c' pc=%04x]\n", m_cycles / CPU_HZ, v, v >= 0x20 && v < 0x7f ? v : '.', cpu.pc()); break;
	case 0xcc: case 0xcd: v = aux.read(port & 1); break;
	case 0xce: case 0xcf: v = term.read(port & 1); break;
	case 0xd2: v = u8(m_par_addr); m_pending &= ~(1u << INT_PARITY); break;
	case 0xd3: v = u8(m_par_addr >> 8); break;
	case 0xe8: v = m_hsi_rbr; m_hsi_da = false; break;
	case 0xe9:
		v = (m_hsi_da ? 0x01 : 0) | (m_hsi_tbre ? 0x02 : 0) | (m_hsi_oe ? 0x24 : 0) |
			((m_led & 0x20) ? 0x40 : 0) | ((hsi_connected && (!hsi_cts || hsi_cts())) ? 0x80 : 0);
		break;
	case 0xea: v = 0x00; break;                // disc flag = 0
	case 0xeb: v = 0x00; break;
	case 0xec: restart(); return 0;            // programmed reset
	case 0xed: v = u8((mode_switch & 0xfc) | 0x03); break;
	case 0xef: v = 0xff; break;
	case 0xf2: v = u8(~(m_ec_pend | (tta_board.requesting() ? 0x04 : 0))); break;   // pending, active low (bit 2 also the TTA's request)
	case 0xe5: v = m_ep85.control_read(); break;
	// E3 bit 4: processor inactive (HALT, or held by an RTPA break)
	case 0xe3: v = ep_present ? u8(0x20 | ((m_e3 & 0x80) && (ep.halted || rtpa_board.stopped() || tta_board.stopped()) ? 0x10 : 0x00)) : 0x00; if (ep_trace) fprintf(stderr, "[%.3f E3 read %02x]\n", m_cycles / CPU_HZ, v); break;
	case 0xf6: case 0xf7: v = 0; break;                     // PC next/last A16-23
	case 0xfc: v = u8(m_lp_pc_next); break;
	case 0xfd: v = u8(m_lp_pc_next >> 8); break;
	case 0xfe: v = u8(m_lp_pc_last); break;
	case 0xff: v = u8(m_lp_pc_last >> 8); break;
	default:
		v = 0x00;                            // nothing decodes the port: reads 00 (diag config probe relies on it)
		break;
	}
	if (trace_io) fprintf(stderr, "[%.3f io rd %02x = %02x @%04x]\r\n", m_cycles / CPU_HZ, port, v, cpu.ppc());
	update_irq();
	return v;
}

void tek8301::io_write(offs_t port, u8 d)
{
	if (trace_io) fprintf(stderr, "[%.3f io wr %02x <- %02x @%04x]\r\n", m_cycles / CPU_HZ, port, d, cpu.ppc());
	if (port >= 0x90 && port <= 0x9f) { m_dma.write(port & 0x0f, d); dma_service(); update_irq(); return; }
	if (port >= 0xc0 && port <= 0xc7 && rtpa_board.present) { m_seconds = m_cycles / CPU_HZ; rtpa_board.write(port & 7, d); update_irq(); return; }
	if (m_ep86.decodes(u8(port))) { m_ep86.io_write(u8(port), d); update_irq(); return; }
	if (port >= 0x70 && port <= 0x77 && tta_board.present) { m_seconds = double(now_cycles()) / CPU_HZ; tta_board.write(port & 7, d, now_cycles()); if (tta_board.take_irq()) tta_interrupt(); update_irq(); return; }
	switch (port) {
	case 0xb8: m_b8 = d; break;
	case 0xb9: m_b9 = d; break;
	case 0xca: case 0xcb: remote.write(port & 1, d); if (rem_trace) fprintf(stderr, "[%.3f REM %s %02x '%c']\n", m_cycles / CPU_HZ, (port & 1) ? "tx" : "cr", d, d >= 0x20 && d < 0x7f ? d : '.'); break;
	case 0xcc: case 0xcd: aux.write(port & 1, d); break;
	case 0xce: case 0xcf: term.write(port & 1, d); break;
	case 0xd2:
		m_d2 = d;
		if (!(d & 1)) m_pending &= ~(1u << INT_REFRESH);
		break;
	case 0xd3: m_d3 = d; break;
	case 0xe7:
		if ((d & 0x80) && !(m_e7 & 0x80)) { m_lp_live = true; }
		m_e7 = d;
		break;
	case 0xf3: m_f3 = d; break;
	case 0xf8: m_f8 = d; break;
	case 0xf4: m_f4 = d; break;
	case 0xf6: m_bp_ext[0] = u8(~d); break;
	case 0xf7: m_bp_ext[1] = u8(~d); break;
	// breakpoint registers are loaded with active-low data (diag writes FD=FF FC=F7 for 0008)
	case 0xfc: m_bp[0] = u16((m_bp[0] & 0xff00) | u8(~d)); break;
	case 0xfd: m_bp[0] = u16((m_bp[0] & 0x00ff) | (u8(~d) << 8)); break;
	case 0xfe: m_bp[1] = u16((m_bp[1] & 0xff00) | u8(~d)); break;
	case 0xff: m_bp[1] = u16((m_bp[1] & 0x00ff) | (u8(~d) << 8)); break;
	case 0xf9:
		m_f9 = d;
		m_ec_pend = 0;                      // a new command releases the latched debug state
		m_svc_stop = false;                 // ... and a slave stopped by an SVC
		rtpa_board.release();               // ... or held by an RTPA break
		tta_board.release();                // ... or a TTA break
		if (ep_trace) fprintf(stderr, "[%.3f F9 <- %02x  FA/FB=%02x%02x F5=%02x E3=%02x]\n", m_cycles / CPU_HZ, d, m_fb, m_fa, m_f5, m_e3);
		if (!(d & 0x01)) { m_fr_pending = true; m_fj_pending = false; m_lp_live = true; }   // FR: forced reset (taken by the slave)
		if (!(d & 0x04) && (d & 0x01)) { m_fj_pending = true; m_lp_live = true; }          // FJ: forced jump (FB:FA, F5); reset wins
		// FI: forced interrupt (D1 = 0). On the Z80 emulator board it is the CPU's NMI
		// (Z80 diag 07/0040: forced interrupt -> PC 0066, HALT there -> 'emulator halted')
		if (!(d & 0x02) && (d & 0x01)) { m_fi_pending = true; m_lp_live = true; }
		break;
	case 0xf5: m_f5 = d; break;
	case 0xe5:
		if (ep_trace) fprintf(stderr, "[%.3f E5 <- %02x]\n", m_cycles / CPU_HZ, d);
		m_ep85.control = d;
		break;
	case 0xe3:
		if (ep_trace) fprintf(stderr, "[%.3f E3 <- %02x]\n", m_cycles / CPU_HZ, d);
		m_e3 = d;
		break;
	case 0xd0:
		if (prom_dos50_fix && prom.present) {
			// PROM software V2.1 (1982) was built for a later DOS/50 V1.x whose resident
			// routines sit 10 bytes higher than in V1.0 (18-AUG-80): 'post event to task'
			// 02FA -> 0304 and 'wake the dispatcher' 03D4 -> 03DE (in V1.0, 0304 is the
			// post's inner entry that needs a message slot in r1, and 03DE is the task
			// entry-address table). Relocate the driver's calls (overlays at 6000-6FFF).
			// Empirically, only the device-write commands (F0-F3: WPROM) need the relocated
			// post on V1.0; status, type, read and compare (PSTAT, PTYPE, RPROM, CPROM) only
			// complete with the original 0304 call. The call sites follow the current command.
			if (peek(0x02fa) == 0xc8 && peek(0x02fb) == 0x52 && peek(0x03d4) == 0x04 && peek(0x03d5) == 0xc0) {
				bool reading = (prom.last_command() & 0xfc) != 0xf0;
				for (offs_t a = 0x6000; a < 0x6ffe; a++) {
					if (peek(a) != 0x3f) continue;
					if (peek(a + 1) == 0x03 && peek(a + 2) == 0xde) poke(a + 2, 0xd4);
					else if (!reading && peek(a + 1) == 0x03 && peek(a + 2) == 0x04) { poke(a + 1, 0x02); poke(a + 2, 0xfa); }
					else if (reading && peek(a + 1) == 0x02 && peek(a + 2) == 0xfa) { poke(a + 1, 0x03); poke(a + 2, 0x04); }
				}
			}
		}
		prom.write(0, d);
		break;
	case 0xd1: prom.write(1, d); break;
	case 0xfa: m_fa = d; break;
	case 0xfb: m_fb = d; break;
	case 0xe8:
		m_hsi_thr = d; m_hsi_tbre = false; m_hsi_tx_timer = hsi_char_cycles;
		break;
	case 0xe9: m_e9 = d; dma_service(); break;
	case 0xea: m_ea = d; break;
	case 0xeb: m_eb_out = d; break;
	case 0xec:
		m_ec = d;
		if (!(d & 1)) m_pending &= ~(1u << INT_TIMER);
		break;
	case 0xed: m_led = d; break;
	case 0xee: m_ee = d; break;
	default: break;
	}
	update_irq();
}

// --------------------------------------------------------------------------
// interrupts

u32 tek8301::level_irqs() const
{
	u32 l = 0;
	if ((m_e9 & 0x02) && m_hsi_da) l |= 1u << INT_HSIN;
	if ((m_e9 & 0x04) && m_hsi_tbre) l |= 1u << INT_HSOUT;
	if (remote.irq()) l |= 1u << INT_REMOTE;
	if (aux.irq()) l |= 1u << INT_AUX;
	if (term.irq()) l |= 1u << INT_TERM;
	if (prom.int13()) l |= 1u << 13;          // PROM programmer: data step
	if (prom.int14()) l |= 1u << 14;          // PROM programmer: status posted
	return l;
}

void tek8301::update_irq()
{
	cpu.set_irq((m_pending | level_irqs()) ? ASSERT_LINE : CLEAR_LINE);
}

int tek8301::intack()
{
	u32 all = m_pending | level_irqs();
	for (int n = 0; n < 32; n++)
		if (all & (1u << n)) {
			if (EDGE_IRQS & (1u << n)) m_pending &= ~(1u << n);
			update_irq();
			static const bool int_trace = getenv("TEK_INTTRACE") != nullptr;
			if (int_trace && n >= 16) fprintf(stderr, "[%.4f 2650 takes INT %d (vector %04x)]\n", m_cycles / CPU_HZ, n, 2 * n);
			return 0x80 | (2 * n);          // ZBSR indirect through page-0 vector 2n
		}
	update_irq();
	return 0x80;
}

// --------------------------------------------------------------------------
// slave (Language Processor / emulator processor) memory accesses: memory map,
// write protection and the prototype bus

u8 tek8301::slave_rd(u16 a, bool m1)
{
	if (proto_all() || (proto_mode() && (m_map[a >> 7] & 1))) return m1 ? target->mem_fetch(a) : target->mem_read(a);
	return prog_rd(PROG_BANK, a);
}

void tek8301::slave_wr(u16 a, u8 d)
{
	const u8 m = m_map[a >> 7];
	const bool user = proto_all() || (proto_mode() && (m & 1));
	// write protection covers program memory (and every block in mode 0); EA<3> enables it
	if ((m_ea & 0x08) && (m & 4) && !user) {
		if (m_ea & 0x80) { m_pending |= 1u << INT_WP; update_irq(); }   // EA<7>: interrupt
		m_wp_hit = true;
		return;                                                          // RAM INH: write suppressed
	}
	if (user) { target->mem_write(a, d); return; }
	prog_wr(PROG_BANK, a, d);
	if (proto_mode()) target->mem_write(a, d);   // mode 1: program-memory writes also reach the prototype
}

// --------------------------------------------------------------------------
// HSI UART + DMA channel 2

void tek8301::hsi_tick(int c)
{
	if (m_hsi_tx_timer > 0 && (m_hsi_tx_timer -= c) <= 0) {
		m_hsi_tx_timer = 0; m_hsi_tbre = true;
		if (hsi_tx) hsi_tx(m_hsi_thr);
	}
	if (m_hsi_rx_gap > 0) m_hsi_rx_gap -= c;
	if (!m_hsi_rxq.empty() && m_hsi_rx_gap <= 0 && !m_hsi_da) {
		m_hsi_rbr = m_hsi_rxq.front(); m_hsi_rxq.pop_front();
		m_hsi_da = true; m_hsi_rx_gap = hsi_char_cycles;
	}
	dma_service();
}

void tek8301::dma_service()
{
	// channel 2 <-> HSI
	if (m_dma.enabled(2)) {
		bool sys = m_e9 & 0x80;
		for (int guard = 0; guard < 4; guard++) {
			if ((m_e9 & 0x10) && m_dma.is_read(2) && m_hsi_tbre && m_hsi_tx_timer == 0 && (!hsi_cts || hsi_cts())) {
				u8 b = mem_rd({ m_dma.ch[2].addr, sys, true });
				m_hsi_thr = b; m_hsi_tbre = false; m_hsi_tx_timer = hsi_char_cycles; dma_transfers++;
				if (m_dma.step(2) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			} else if ((m_e9 & 0x20) && m_dma.is_write(2) && m_hsi_da) {
				mem_wr({ m_dma.ch[2].addr, sys, true }, m_hsi_rbr); m_hsi_da = false; dma_transfers++;
				if (m_dma.step(2) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			} else break;
			if (!m_dma.enabled(2)) break;
		}
	}
	// channels 0 -> 1: memory to memory through a latch
	if (m_dma.enabled(0) && m_dma.enabled(1)) {
		bool src_sys = m_e9 & 0x01, dst_sys = m_e9 & 0x40;
		static const bool dmatrace = getenv("TEK_DMATRACE") != nullptr;
		if (dmatrace) {
			u32 n = (m_dma.ch[0].count & 0x3fff) + 1;
			fprintf(stderr, "[%.3f dma %s %04x -> %s %04x len %u:", m_cycles / CPU_HZ, src_sys ? "sys" : "prog", m_dma.ch[0].addr,
				dst_sys ? "sys" : "prog", m_dma.ch[1].addr, n);
			for (u32 i = 0; i < n && i < 64; i++) fprintf(stderr, " %02x", mem_rd({ u16(m_dma.ch[0].addr + i), src_sys, true }, false));
			fprintf(stderr, "]\n");
		}
		for (int i = 0; i < 0x4000 && m_dma.enabled(0) && m_dma.enabled(1); i++) {
			u8 b = mem_rd({ m_dma.ch[0].addr, src_sys, true });
			mem_wr({ m_dma.ch[1].addr, dst_sys, true }, b); dma_transfers++;
			bool tc0 = m_dma.step(0), tc1 = m_dma.step(1);
			if ((tc0 || tc1) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			if (tc0 || tc1) break;
		}
	}
}

// --------------------------------------------------------------------------
// time

void tek8301::run_slice(int cycles)
{
	const u64 timer_period = u64(CPU_HZ / 10);      // 100 ms
	const u64 refresh_period = u64(CPU_HZ / 500);   // diag refresh interrupt (2 ms, guess)
	int used = 0;
	while (used < cycles) {
		int chunk = std::min(cycles - used, 100);
		m_chunk = chunk; m_in_cpu = true;
		int c = cpu.run(chunk);
		m_in_cpu = false;
		if (c <= 0) c = chunk;
		lp_run(c);
		m_ep86.run(c);
		used += c; m_cycles += c;
		if (tta_board.present) tta_board.sync(m_cycles);
		remote.tick(c); aux.tick(c); term.tick(c);
		hsi_tick(c);
		prom.tick(c);
		if (m_cycles >= m_next_timer) {
			m_next_timer = m_cycles + timer_period;
			if (m_ec & 1) m_pending |= 1u << INT_TIMER;
		}
		if (m_cycles >= m_next_refresh) {
			m_next_refresh = m_cycles + refresh_period;
			if (m_d2 & 1) m_pending |= 1u << INT_REFRESH;
		}
		update_irq();
	}
}
