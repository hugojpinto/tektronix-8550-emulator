// Tektronix 8501 Data Management Unit -- see tek8501.h and notes/8501.md
#include "machine/tek8501.h"
#include "machine/imd.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr offs_t RAM_TOP      = 0170000;   // first non-RAM byte address (top of RAM = 167776)
constexpr offs_t PROM_BASE    = 0173000;
constexpr offs_t PROM_END     = 0175000;
constexpr offs_t LTC_ADDR     = 0177546;
constexpr offs_t CBPR_ADDR    = 0177160;
constexpr offs_t HSI_BASE     = 0176510;   // I/O board, 8 words assumed (176510-176526)
constexpr u16    FD_VECTOR    = 0230;
constexpr u16    HSI_VECTOR   = 0360;
constexpr u16    DSDD_RECORDS = 3991;
constexpr u16    FD_FW_VERSION = 210;      // reported as version x 100 (unknown real value)

bool read_file(const std::string &p, std::vector<u8> &v)
{
	FILE *f = fopen(p.c_str(), "rb"); if (!f) return false;
	int c; v.clear(); while ((c = fgetc(f)) != EOF) v.push_back(u8(c)); fclose(f); return true;
}
}

tek8501::tek8501()
	: cpu(*this)
	, aux(0177560, 060)
	, printer(0177510, 0200)
	, m_ram(RAM_TOP / 2, 0)
	, m_ltc(0)
{
	memset(m_prom, 0xff, sizeof m_prom);
}

bool tek8501::load_roms(const std::string &dir, std::string &err)
{
	std::vector<u8> lo, hi;
	if (!read_file(dir + "/160-0399-01.u5080", lo) || !read_file(dir + "/160-0400-00.u5090", hi) ||
		lo.size() < 1024 || hi.size() < 1024) {
		err = "missing 8501 utility PROMs (160-0399-01.u5080 / 160-0400-00.u5090) in " + dir;
		return false;
	}
	// two 1Kx8 PROMs in parallel = 1 KW; LTC<5> selects which 512-word half is
	// visible at 173000: 0 = diagnostics (first half), 1 = bootstrap (second half)
	for (int h = 0; h < 2; h++)
		for (int w = 0; w < 512; w++)
			m_prom[h][w] = u16(lo[h * 512 + w]) | (u16(hi[h * 512 + w]) << 8);
	return true;
}

bool tek8501::mount(int drive, const std::string &path, std::string &err)
{
	drive_t &d = m_drive[drive & 3];
	if (!load_imd_linear(path, d.data, err)) return false;
	d.path = path; d.present = true; d.changed = true;
	if (d.data.size() < size_t(DSDD_RECORDS) * 256) d.data.resize(size_t(DSDD_RECORDS) * 256, 0xe5);
	return true;
}

void tek8501::power_on()
{
	std::fill(m_ram.begin(), m_ram.end(), 0);
	restart();
}

void tek8501::restart()
{
	bus_reset();
	m_ltc = 0;                       // INIT: LEDs all on, PROM = diagnostics
	cpu.reset(PROM_BASE, 0340);
}

void tek8501::bus_reset()
{
	aux.reset();
	printer.reset();
	m_ltc &= ~0140;                  // INIT clears IE and PROM select (LEDs set on)
	m_ltc &= 003777;
	m_fd_busy = false; m_fd_irq = false;
	memset(m_hsi, 0, sizeof m_hsi);
	cpu.set_input(t11_device::VEC_LINE, CLEAR_LINE);
}

// --------------------------------------------------------------------------
// bus

u16 tek8501::read16(offs_t a, bool &nxm)
{
	a &= 0xfffe;
	if (a < RAM_TOP) return m_ram[a >> 1];
	if (a >= PROM_BASE && a < PROM_END) return m_prom[(m_ltc >> 5) & 1][(a - PROM_BASE) >> 1];
	if (aux.decodes(a)) { u16 v = aux.read(a); update_irq(); return v; }
	if (printer.decodes(a)) { u16 v = printer.read(a); update_irq(); return v; }
	if (a == LTC_ADDR) return m_ltc & 0000341;     // LEDs are write-only
	if (a == CBPR_ADDR) return m_cbpr;
	if (a >= HSI_BASE && a < HSI_BASE + 16) return m_hsi[(a - HSI_BASE) >> 1];
	nxm = true;
	return 0;
}

void tek8501::write16(offs_t a, u16 d, bool &nxm)
{
	a &= 0xfffe;
	if (a < RAM_TOP) { m_ram[a >> 1] = d; return; }
	if (a >= PROM_BASE && a < PROM_END) return;        // ROM: ignore
	if (aux.decodes(a)) { aux.write(a, d); if ((a & 6) == 4) aux.write(a, d, true); update_irq(); return; }
	if (printer.decodes(a)) { printer.write(a, d); if ((a & 6) == 4) printer.write(a, d, true); update_irq(); return; }
	if (a == LTC_ADDR) {
		u16 mon = m_ltc & 0200;
		if (!(d & 0200)) mon = 0;                      // writing 0 clears monitor, 1 doesn't set
		m_ltc = (d & 0174140) | mon | (m_ltc & 1);
		return;
	}
	if (a == CBPR_ADDR) { fd_start(d); return; }
	if (a >= HSI_BASE && a < HSI_BASE + 16) { m_hsi[(a - HSI_BASE) >> 1] = d; return; }
	nxm = true;
}

void tek8501::write8(offs_t a, u8 d, bool &nxm)
{
	a &= 0xffff;
	if (a < RAM_TOP) {
		u16 &w = m_ram[a >> 1];
		w = (a & 1) ? u16((w & 0x00ff) | (d << 8)) : u16((w & 0xff00) | d);
		return;
	}
	// devices: read-modify-write of the word (DATOB on Q-bus)
	offs_t wa = a & 0xfffe;
	if (aux.decodes(wa) || printer.decodes(wa)) {
		serial_port &p = aux.decodes(wa) ? aux : printer;
		if (a & 1) p.write(wa, u16(d) << 8, true); else p.write(wa, d);
		update_irq();
		return;
	}
	bool n2 = false;
	u16 cur = read16(wa, n2);
	if (n2) { nxm = true; return; }
	if (wa == LTC_ADDR) {
		// high byte = LEDs only; low byte = control bits (bit 7: 0 clears monitor).
		// For a high-byte write, pass monitor=1 so it is not cleared.
		u16 v = (a & 1) ? u16((d << 8) | (m_ltc & 0x007f) | 0200) : u16((m_ltc & 0xff00) | d);
		write16(wa, v, nxm);
		return;
	}
	u16 v = (a & 1) ? u16((cur & 0x00ff) | (d << 8)) : u16((cur & 0xff00) | d);
	write16(wa, v, nxm);
}

u16 tek8501::peek16(offs_t a)
{
	a &= 0xfffe;
	if (a < RAM_TOP) return m_ram[a >> 1];
	if (a >= PROM_BASE && a < PROM_END) return m_prom[(m_ltc >> 5) & 1][(a - PROM_BASE) >> 1];
	if (a == LTC_ADDR) return m_ltc;
	if (a == CBPR_ADDR) return m_cbpr;
	return 0;
}

void tek8501::poke16(offs_t a, u16 v)
{
	if ((a & 0xfffe) < RAM_TOP) m_ram[(a & 0xfffe) >> 1] = v;
}

// --------------------------------------------------------------------------
// interrupts: Q-bus daisy chain order (closest to CPU first) -- assumed:
// floppy controller, I/O board (HSI), AUX port, PRINTER port

void tek8501::update_irq()
{
	bool any = m_fd_irq || aux.irq() || printer.irq();
	cpu.set_input(t11_device::VEC_LINE, any ? ASSERT_LINE : CLEAR_LINE);
}

int tek8501::iack()
{
	int v = -1;
	if (m_fd_irq) { m_fd_irq = false; v = FD_VECTOR; }
	else if (aux.irq()) v = aux.ack();
	else if (printer.irq()) v = printer.ack();
	update_irq();
	return v;
}

// --------------------------------------------------------------------------
// time

void tek8501::run_slice(int cycles)
{
	const u64 ltc_period = u64(CPU_HZ / 60.0);
	int used = 0;
	while (used < cycles) {
		int chunk = std::min(cycles - used, 200);
		int c = cpu.halted() ? chunk : cpu.run(chunk);
		if (c <= 0) c = chunk;
		used += c;
		m_cycles += c;
		aux.tick(c);
		printer.tick(c);
		if (m_fd_busy && m_cycles >= m_fd_done_at) fd_execute();
		if (m_cycles >= m_next_ltc) {
			m_next_ltc = m_cycles + ltc_period;
			m_ltc |= 0200;                            // monitor bit
			if (m_ltc & 0100) cpu.set_input(t11_device::EVENT_LINE, ASSERT_LINE);
			cpu.set_input(t11_device::EVENT_LINE, CLEAR_LINE);
		}
		update_irq();
		if (cpu.halted() && m_odt_open == -1 && m_odt_buf.empty()) { /* idle in ODT */ }
	}
}

// --------------------------------------------------------------------------
// flexible disc controller, high-level

void tek8501::fd_start(u16 cbpr)
{
	m_cbpr = cbpr;
	if (m_fd_busy) {
		// command while busy: flag both blocks, abort
		mem_wr(cbpr + 8, mem_rd(cbpr + 8) | 0100100);
		mem_wr(cbpr, mem_rd(cbpr) | 0100000);
		m_fd_busy = false;
		return;
	}
	m_fd_busy = true;
	m_fd_done_at = m_cycles + 2000;   // ~1 ms; real seeks take longer
}

void tek8501::fd_execute()
{
	m_fd_busy = false;
	u32 blk = m_cbpr;
	for (int chain = 0; chain < 64; chain++) {
		u16 cr  = mem_rd(blk + 0);
		u16 wc  = mem_rd(blk + 2);
		u16 da  = mem_rd(blk + 4);
		u16 dma = mem_rd(blk + 6);
		int drv = (cr >> 10) & 3;
		int cmd = cr & 077;
		u32 page = u32((cr >> 8) & 3) << 16;
		drive_t &d = m_drive[drv];
		u16 er = 0, sr = 0;

		if (d.present) sr |= 0040000 | 0004000;                  // ready, two-sided
		if (d.changed) { sr |= 0010000; if (cr & 010000) er |= 0000002; }
		d.changed = false;

		auto need_ready = [&]() { if (!d.present) { er |= 0100001; return false; } return true; };

		switch (cmd) {
		case 041: case 043: case 045: case 061: {   // read / write / verify / write control field
			if (!need_ready() || (er & 2)) break;
			u32 words = wc;                          // 0 = seek
			if (cmd == 045 && wc == 0177777) words = (DSDD_RECORDS - da) * 128u;
			u32 recs = (words + 127) / 128;
			if (u32(da) + recs > DSDD_RECORDS) { er |= 0110000; break; }
			if (cmd != 045 && words && u32(dma) + words * 2 > 0x10000) { er |= 0102000; break; }
			for (u32 w = 0; w < words; w++) {
				size_t off = size_t(da) * 256 + w * 2;
				u32 ma = page | ((dma + w * 2) & 0xffff);
				if (cmd == 041) mem_wr(ma, u16(d.data[off] | (d.data[off + 1] << 8)));
				else if (cmd == 043 || cmd == 061) { u16 v = mem_rd(ma); d.data[off] = u8(v); d.data[off + 1] = u8(v >> 8); }
			}
			if ((cmd == 043 || cmd == 061) && (words % 128)) {   // zero-fill partial record
				size_t off = size_t(da) * 256 + words * 2, end = size_t(da + recs) * 256;
				for (; off < end; off++) d.data[off] = 0;
			}
			if (cr & 020000) {
				mem_wr(blk + 2, 0);
				mem_wr(blk + 4, u16(da + recs));
				mem_wr(blk + 6, u16(dma + words * 2));
			}
			break;
		}
		case 047: case 051:                          // format DD / SD
			if (!need_ready()) break;
			if (cr & 020000) { mem_wr(blk + 2, 0); mem_wr(blk + 4, 154); }
			break;
		case 053: case 055:                          // status / slow status
			mem_wr(blk + 2, FD_FW_VERSION);
			mem_wr(blk + 6, 0);
			mem_wr(blk + 4, d.present ? DSDD_RECORDS : 0);
			if (!d.present) er |= 0100001;
			break;
		case 057:                                    // initialize
			if (!d.present) er |= 0100004;           // self-test error: drive not present
			sr |= 0100000;                           // track 00
			if (cr & 020000) { mem_wr(blk + 2, 0); mem_wr(blk + 4, 0); mem_wr(blk + 6, 0); }
			break;
		case 063:                                    // diagnostic: accept, do nothing
			break;
		default:
			er |= 0104000;                           // invalid command
		}
		if (er & 0077777) er |= (er & 0157777) ? 0100000 : 0;
		mem_wr(blk + 8, er);
		mem_wr(blk + 10, sr);
		mem_wr(blk + 12, 0);
		mem_wr(blk, cr | 0100000);                   // DONE

		u16 chr = mem_rd(blk + 14);
		bool hard = er & 0100000;
		if ((cr & 0100) && !(hard && !(chr & 1))) { blk = chr & 0xfffe; continue; }
		if ((cr & 040000) && (!(cr & 0100) || hard)) m_fd_irq = true;
		break;
	}
	update_irq();
}

// --------------------------------------------------------------------------
// ODT (subset of the LSI-11 console microcode)
//   nnnnnn/  open location     Rn/ or $n/ open register     RS/ open PSW
//   <CR> close   <LF> close and open next   nnnnnnG go   P proceed

void tek8501::odt_enter()
{
	m_odt_buf.clear(); m_odt_open = -1;
	odt_out(util::string_format("\r\n%06o\r\n@", cpu.reg(7)));
}

void tek8501::odt_show_open()
{
	u16 v;
	if (m_odt_open >= 0) v = peek16(offs_t(m_odt_open));
	else if (m_odt_open == -10) v = cpu.psw();
	else v = cpu.reg(-2 - m_odt_open);
	odt_out(util::string_format("%06o ", v));
}

void tek8501::odt_char(u8 c)
{
	c &= 0177;
	if (c >= 'a' && c <= 'z') c -= 040;
	auto num = [&](bool &ok) { ok = !m_odt_buf.empty(); return u16(strtoul(m_odt_buf.c_str(), nullptr, 8)); };
	auto store = [&]() {
		bool ok; u16 v = num(ok);
		if (!ok || m_odt_open == -1) return;
		if (m_odt_open >= 0) { bool n = false; write16(offs_t(m_odt_open), v, n); }
		else if (m_odt_open == -10) cpu.set_psw(v);
		else cpu.set_reg(-2 - m_odt_open, v);
	};
	aux.host_tx_direct(c == '\r' ? '\r' : c);
	if ((c >= '0' && c <= '7') || c == 'R' || c == '$' || c == 'S') { m_odt_buf += char(c); return; }
	switch (c) {
	case '/': {
		if (!m_odt_buf.empty() && (m_odt_buf[0] == 'R' || m_odt_buf[0] == '$')) {
			if (m_odt_buf.size() > 1 && m_odt_buf[1] == 'S') m_odt_open = -10;
			else m_odt_open = -2 - ((m_odt_buf.size() > 1 ? m_odt_buf[1] - '0' : 0) & 7);
		} else { bool ok; u16 a = num(ok); if (ok) m_odt_open = a & 0xfffe; }
		m_odt_buf.clear();
		if (m_odt_open != -1) odt_show_open();
		break;
	}
	case '\r':
		store(); m_odt_buf.clear(); m_odt_open = -1; odt_out("\r\n@");
		break;
	case '\n':
		store(); m_odt_buf.clear();
		if (m_odt_open >= 0) { m_odt_open = (m_odt_open + 2) & 0xfffe; odt_out(util::string_format("\r\n%06o/", m_odt_open)); odt_show_open(); }
		else if (m_odt_open <= -2 && m_odt_open > -9) { m_odt_open = -2 - ((-2 - m_odt_open + 1) & 7); odt_out(util::string_format("\r\nR%d/", -2 - m_odt_open)); odt_show_open(); }
		else odt_out("\r\n@");
		break;
	case 'G': {
		bool ok; u16 a = num(ok); m_odt_buf.clear();
		bus_reset();
		cpu.set_reg(7, ok ? a : cpu.reg(7));
		cpu.set_psw(0);
		odt_out("\r\n");
		cpu.set_halted(false);
		break;
	}
	case 'P':
		m_odt_buf.clear(); odt_out("\r\n"); cpu.set_halted(false);
		break;
	default:
		m_odt_buf.clear(); odt_out("?\r\n@");
	}
}
