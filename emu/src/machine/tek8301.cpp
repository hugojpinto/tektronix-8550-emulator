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
constexpr int INT_SVC1 = 16, INT_BP1 = 24, INT_BP2 = 25, INT_SCY = 26, INT_SLV_HALT = 27, INT_DIAG = 28;
}

tek8301::tek8301()
	: cpu(*this), m_sysram(0x10000, 0), m_progmem(0x10000, 0), m_par(0x10000, 0)
{
	lp_reset();
}

// --------------------------------------------------------------------------
// Language Processor (Z80A, slave) + Emulator Controller

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
		ec_interrupt(INT_SVC1, false);           // "registers loaded"
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
		ec_interrupt(INT_SVC1, false);           // "registers saved"
	} else if (entry == 0xf800) {                // PUT: write block[0] at block[1]:block[2], verify -> block[3] = 0 ok
		u16 a = u16((pm(1) << 8) | pm(2));
		target->mem_write(a, pm(0));
		m_progmem[3] = (target->mem_read(a) == pm(0)) ? 0x00 : 0x01;   // DOS/50 MOV: nonzero = error 3C
		ec_interrupt(INT_SVC1, false);
	} else if (entry == 0xf803) {                // GET: read prototype memory at block[1]:block[2] into block[0]
		m_progmem[0] = target->mem_read(u16((pm(1) << 8) | pm(2)));
		m_progmem[3] = 0x00;
		ec_interrupt(INT_SVC1, false);
	} else if (ep_trace) fprintf(stderr, "[EP firmware entry %04x unknown]\n", entry);
	if (ep_trace) {
		fprintf(stderr, "[   block now:");
		for (int i = 0; i < 27; i++) fprintf(stderr, " %02x", pm(i));
		fprintf(stderr, "]\n");
	}
}

u8 tek8301::lp_in(z80 *z, u8 port)
{
	tek8301 *m = static_cast<tek8301 *>(z->userdata);
	if (z == &m->ep && m->proto_mode()) return m->target->io_read(port);
	return 0xff;
}

void tek8301::lp_out(z80 *z, u8 port, u8 d)
{
	tek8301 *m = static_cast<tek8301 *>(z->userdata);
	// SVC: OUT to the 8-port block selected by the SVC map (F3 D3-D0 = A7-A4, default F0-F7).
	// SVC n is OUT (F8h-n): F7 = SVC 1 ... F0 = SVC 8 (Z80 Emulator Specifics, Table 7C-3)
	if ((m->m_f9 & 0x10) && (port & 0xf8) == ((m->m_f3 & 0x0f) << 4)) {
		m->ec_interrupt(INT_SVC1 + (7 - (port & 7)));
		return;
	}
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
	// F2 pending register bits (read active low): 7 BP1 6 BP2 5 S/CY 4 HALT 3 DIAG
	switch (n) {
	case INT_BP1: m_ec_pend |= 0x80; break;
	case INT_BP2: m_ec_pend |= 0x40; break;
	case INT_SCY: m_ec_pend |= 0x20; break;
	case INT_SLV_HALT: m_ec_pend |= 0x10; break;
	case INT_DIAG: m_ec_pend |= 0x08; break;
	default: break;
	}
	if (latch_pc) m_lp_pc_next = slave().pc;
	if (m_f9 & 0x08)                    // IM = 1: debug interrupts not masked
		m_pending |= 1u << n;
	update_irq();
}

void tek8301::lp_run(int cycles2650)
{
	// Master halted but no slave able to run: the Emulator Controller reports the
	// slave as halted at once ("Processor Inactive Test", 070-2976 §4).
	// (Only when no slave is enabled at all -- not when an enabled slave is merely
	// blocked by an outstanding SVC/debug interrupt.)
	if (cpu.halted() && !slave_enabled() && !(m_ec_pend & 0x10)) { ec_interrupt(INT_SLV_HALT); return; }
	if (!slave_enabled() || !cpu.halted()) return;
	z80 &z = slave();
	// forced reset / forced jump are latched by F9 and taken by the slave when it runs
	if (m_fr_pending) { m_fr_pending = false; z80 keep = z; z80_setup(z); z.read_byte = keep.read_byte; m_ec_pend = 0; }
	if (m_fj_pending) {
		// jump address registers FA/FB/F5 are loaded active-low (DOS/50 writes FB:FA = FEFF
		// to start a program at 0100, and FF:07F9 for the board firmware at 00:F806)
		m_fj_pending = false;
		u16 target = u16(~(m_fa | (m_fb << 8)));
		u8 ext = u8(~m_f5);
		if (ep_trace) fprintf(stderr, "[%.3f forced jump %02x:%04x -> %s]\n", m_cycles / CPU_HZ, ext, target, &z == &ep ? "EP" : "LP");
		z.halted = false;
		z.pc = target;
		if (&z == &ep && target >= 0xf800) { ep_firmware(target); return; }   // board firmware space
	}
	if (!lp_running()) return;
	m_lp_frac += cycles2650 * ((&z == &lp && (m_e7 & 0x04)) ? 1.25 : 2.0);   // Z80 at 2.5 or 4 MHz vs 2650 at 2 MHz
	while (m_lp_frac > 0 && lp_running()) {
		unsigned long c0 = z.cyc;
		m_lp_pc_last = z.pc;
		if (lp_trace_hook) lp_trace_hook(z.pc);
		u32 before = m_pending;
		m_bp_hit = 0;
		if (&z == &ep && proto_mode()) {
			target->poll();
			bool n = target->nmi();
			if (n && !m_nmi_prev) z80_gen_nmi(&z);
			m_nmi_prev = n;
			if (target->irq() && z.iff1) z80_gen_int(&z, target->irq_vector());
		}
		z80_step(&z);
		m_lp_frac -= double(z.cyc - c0);
		if (m_pending != before) break;         // SVC / write-protect violation (immediate): master takes over
		// "pending" debug interrupts are issued at the next fetch, after PC next is latched
		// (priority SVC > BP1 > BP2 > S/CY > HALT)
		if (m_bp_hit & 1) { ec_interrupt(INT_BP1); break; }
		if (m_bp_hit & 2) { ec_interrupt(INT_BP2); break; }
		if (!(m_f8 & 0x10)) { ec_interrupt(INT_SCY); break; }
		if (z.halted) { ec_interrupt(INT_SLV_HALT); break; }
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
	remote.write(0, 3); aux.write(0, 3); term.write(0, 3);
	m_dma.reset();
	m_hsi_da = false; m_hsi_oe = false; m_hsi_tbre = true; m_hsi_tx_timer = 0;
	m_ec_pend = 0; m_f3 = 0x0f; m_f8 = 0x1f; m_f9 = 0x1f; m_lp_live = false; m_f4 = 0xff; m_e3 = 0; m_fr_pending = m_fj_pending = false; m_f5 = 0xff;
	lp_reset();
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
	else switch (port) {
	case 0xb8: v = m_b8 & 0x5f; break;
	case 0xb9: v = m_b9 & 0x5f; break;
	case 0xca: case 0xcb: v = remote.read(port & 1); break;
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
	case 0xf2: v = u8(~m_ec_pend); break;                 // pending, active low
	case 0xe3: v = ep_present ? u8((m_e3 & 0x80) && ep.halted ? 0x10 : 0x00) : 0x00; if (ep_trace) fprintf(stderr, "[%.3f E3 read %02x]\n", m_cycles / CPU_HZ, v); break;
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
	switch (port) {
	case 0xb8: m_b8 = d; break;
	case 0xb9: m_b9 = d; break;
	case 0xca: case 0xcb: remote.write(port & 1, d); break;
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
		if (ep_trace) fprintf(stderr, "[%.3f F9 <- %02x  FA/FB=%02x%02x F5=%02x E3=%02x]\n", m_cycles / CPU_HZ, d, m_fb, m_fa, m_f5, m_e3);
		if (!(d & 0x01)) { m_fr_pending = true; m_fj_pending = false; m_lp_live = true; }   // FR: forced reset (taken by the slave)
		if (!(d & 0x04) && (d & 0x01)) { m_fj_pending = true; m_lp_live = true; }          // FJ: forced jump (FB:FA, F5); reset wins
		break;
	case 0xf5: m_f5 = d; break;
	case 0xe3:
		if (ep_trace) fprintf(stderr, "[%.3f E3 <- %02x]\n", m_cycles / CPU_HZ, d);
		m_e3 = d;
		break;
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
			return 0x80 | (2 * n);          // ZBSR indirect through page-0 vector 2n
		}
	update_irq();
	return 0x80;
}

// --------------------------------------------------------------------------
// slave (Language Processor / emulator processor) memory accesses: memory map,
// write protection and the prototype bus

u8 tek8301::slave_rd(u16 a)
{
	if (proto_all() || (proto_mode() && (m_map[a >> 7] & 1))) return target->mem_read(a);
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
				m_hsi_thr = b; m_hsi_tbre = false; m_hsi_tx_timer = hsi_char_cycles;
				if (m_dma.step(2) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			} else if ((m_e9 & 0x20) && m_dma.is_write(2) && m_hsi_da) {
				mem_wr({ m_dma.ch[2].addr, sys, true }, m_hsi_rbr); m_hsi_da = false;
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
			mem_wr({ m_dma.ch[1].addr, dst_sys, true }, b);
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
		int c = cpu.run(chunk);
		if (c <= 0) c = chunk;
		lp_run(c);
		used += c; m_cycles += c;
		remote.tick(c); aux.tick(c); term.tick(c);
		hsi_tick(c);
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
