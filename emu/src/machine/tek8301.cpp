// Tektronix 8301 MDU -- see tek8301.h and notes/8301.md
#include "machine/tek8301.h"
#include <cstdio>

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
constexpr u32 EDGE_IRQS = (1u << INT_TIMER) | (1u << INT_DMA) | (1u << INT_REFRESH) | (1u << INT_PARITY) | (1u << INT_WP);
}

tek8301::tek8301()
	: cpu(*this), m_sysram(0x10000, 0), m_progmem(0x10000, 0), m_par(0x10000, 0)
{
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
	if (b.sys) {
		u16 a = u16(b.a);
		// System RAM parity: a byte written under one complement setting and read
		// under the other fails the check (used by the power-up parity test).
		if (ovl && (m_d2 & 2) && m_par[a] != ((m_d2 >> 2) & 1)) {
			if (!(m_pending & (1u << INT_PARITY))) m_par_addr = a;
			m_pending |= 1u << INT_PARITY;
			update_irq();
		}
		return m_sysram[a];
	}
	return m_progmem[b.a & 0xffff];
}

void tek8301::mem_wr(busaddr b, u8 d)
{
	if (b.sys) { m_sysram[b.a & 0xffff] = d; m_par[b.a & 0xffff] = (m_d2 >> 2) & 1; }
	else m_progmem[b.a & 0xffff] = d;
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
	default:
		if (port >= 0xf0) v = 0xff;           // Emulator Controller: not present yet
		break;
	}
	if (trace_io) fprintf(stderr, "[io rd %02x = %02x @%04x]\r\n", port, v, cpu.ppc());
	update_irq();
	return v;
}

void tek8301::io_write(offs_t port, u8 d)
{
	if (trace_io) fprintf(stderr, "[io wr %02x <- %02x @%04x]\r\n", port, d, cpu.ppc());
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
	case 0xe7: m_e7 = d; break;
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
				u8 b = mem_rd({ m_dma.ch[2].addr, sys });
				m_hsi_thr = b; m_hsi_tbre = false; m_hsi_tx_timer = hsi_char_cycles;
				if (m_dma.step(2) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			} else if ((m_e9 & 0x20) && m_dma.is_write(2) && m_hsi_da) {
				mem_wr({ m_dma.ch[2].addr, sys }, m_hsi_rbr); m_hsi_da = false;
				if (m_dma.step(2) && (m_e9 & 0x08)) m_pending |= 1u << INT_DMA;
			} else break;
			if (!m_dma.enabled(2)) break;
		}
	}
	// channels 0 -> 1: memory to memory through a latch
	if (m_dma.enabled(0) && m_dma.enabled(1)) {
		bool src_sys = m_e9 & 0x01, dst_sys = m_e9 & 0x40;
		for (int i = 0; i < 0x4000 && m_dma.enabled(0) && m_dma.enabled(1); i++) {
			u8 b = mem_rd({ m_dma.ch[0].addr, src_sys });
			mem_wr({ m_dma.ch[1].addr, dst_sys }, b);
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
