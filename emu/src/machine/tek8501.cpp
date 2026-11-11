// Tektronix 8501 Data Management Unit -- see tek8501.h and notes/8501.md
#include "machine/tek8501.h"
#include "machine/imd.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const bool g_iotrace = getenv("TEK_IOTRACE") != nullptr;

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
	aux.char_cycles = printer.char_cycles = int(CPU_HZ * 10 / 9600);   // 9600 baud, 10 bits/char
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

bool tek8501::mount(int drive, const std::string &path, std::string &err, bool writeback)
{
	drive_t &d = m_drive[drive & 3];
	if (!load_imd_linear(path, d.data, err, &d.is_imd, &d.comment)) return false;
	d.path = path; d.present = true; d.changed = true; d.writeback = writeback; d.dirty = false;
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
	io_reset(true);
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
	if (a >= HSI_BASE && a < HSI_BASE + 16) { u16 v = io_read((a - HSI_BASE) >> 1); update_irq(); return v; }
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
		if (g_iotrace && ((d ^ m_ltc) & 0100)) fprintf(stderr, "[8501 %.3fs ltc ie=%d pc=%06o]\n", m_cycles / CPU_HZ, (d >> 6) & 1, cpu.ppc());
		u16 mon = m_ltc & 0200;
		if (!(d & 0200)) mon = 0;                      // writing 0 clears monitor, 1 doesn't set
		m_ltc = (d & 0174140) | mon | (m_ltc & 1);
		return;
	}
	if (a == CBPR_ADDR) { fd_start(d); return; }
	if (a >= HSI_BASE && a < HSI_BASE + 16) { io_write((a - HSI_BASE) >> 1, d); update_irq(); return; }
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
	bool any = m_fd_irq || io_irq() || aux.irq() || printer.irq();
	cpu.set_input(t11_device::VEC_LINE, any ? ASSERT_LINE : CLEAR_LINE);
}

int tek8501::iack()
{
	int v = -1;
	if (m_fd_irq) { m_fd_irq = false; v = FD_VECTOR; }
	else if (m_io.rx_req) { m_io.rx_req = false; v = m_io.vec & 0360; }
	else if (m_io.tx_req) { m_io.tx_req = false; v = (m_io.vec & 0360) + 4; }
	else if (m_io.dma_req) { m_io.dma_req = false; v = (m_io.vec & 0360) + 010; }
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
		io_tick(c);
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
	m_fd_done_at = m_cycles + u64(CPU_HZ / 1000);   // ~1 ms; real seeks take longer
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

		if (g_iotrace && !(cmd == 041 || cmd == 043 || cmd == 045 || cmd == 061)) fprintf(stderr, "[8501 %.3fs fd cmd %02o drv %d cr %06o wc %06o da %06o dma %06o]\n", m_cycles / CPU_HZ, cmd, drv, cr, wc, da, dma);
		auto need_ready = [&]() { if (!d.present) { er |= 0100001; return false; } return true; };

		switch (cmd) {
		case 041: case 043: case 045: case 061: {   // read / write / verify / write control field
			if (g_iotrace) fprintf(stderr, "[8501 %.3fs fd cmd %02o drv %d rec %u words %u dma %06o]\n", m_cycles / CPU_HZ, cmd, drv, da, wc, dma);
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
				else if (cmd == 043 || cmd == 061) { u16 v = mem_rd(ma); d.data[off] = u8(v); d.data[off + 1] = u8(v >> 8); d.dirty = true; d.last_write = m_cycles; }
			}
			if ((cmd == 043 || cmd == 061) && (words % 128)) {   // zero-fill partial record
				size_t off = size_t(da) * 256 + words * 2, end = size_t(da + recs) * 256;
				for (; off < end; off++) d.data[off] = 0;
				d.dirty = true; d.last_write = m_cycles;
			}
			if (cr & 020000) {
				mem_wr(blk + 2, 0);
				mem_wr(blk + 4, u16(da + recs));
				mem_wr(blk + 6, u16(dma + words * 2));
			}
			break;
		}
		case 047: case 051: {                        // format DD / SD, FDDAR = track (0-153)
			// DOS/50 FORMAT formats track 0, 1, 2 ... until the controller refuses
			// a track past the end, so that must be an error (capacity overflow, as
			// for records). Track 0 = cyl 0 side 0 (13 records), then 26 per track.
			if (!need_ready()) break;
			if (da >= 154) { er |= 0110000; break; }
			size_t first = da == 0 ? 0 : 13 + size_t(da - 1) * 26, n = da == 0 ? 13 : 26;
			std::fill(d.data.begin() + first * 256, d.data.begin() + (first + n) * 256, u8(0));
			d.dirty = true; d.last_write = m_cycles;
			if (cr & 020000) { mem_wr(blk + 2, 0); mem_wr(blk + 4, u16(da + 1)); }
			break;
		}
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
		if (g_iotrace && er) fprintf(stderr, "[8501 fd cmd %02o drv %d -> FDER %06o]\n", cmd, drv, er);
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

// --------------------------------------------------------------------------
// I/O board: HSI UART + DMA (Section 10 of 061-2355-00)
//   +0 CSR: 15 DERR 14 TOR 13 AOF 12 ABORT 11 RBE 10 INIT(strobe) 8 RBEE 7 DONE 6 IE
//           5-4 XBA17-16 3 SWAP 2 PACKED 1 OUT (1 = from memory, inferred) 0 GO
//   +2 CTR (2's complement byte count)  +4 ADDR  +6 VEC (7-4)
//   +10 RCSR: 7 DONE 6 IE 0 NCTS   +12 RBUF   +14 XCSR: 7 RDY 6 IE 0 BREAK   +16 XBUF

void tek8501::io_reset(bool bus)
{
	m_io.csr = 0; m_io.rcsr = 0; m_io.xcsr = 0200;
	m_io.tx_timer = 0; m_io.rx_req = m_io.tx_req = m_io.dma_req = false;
	if (bus) { m_io.vec = 060; }
}

u16 tek8501::io_read(int reg)
{
	if (g_iotrace) fprintf(stderr, "[8501 %.3fs io rd %d pc=%06o]\n", m_cycles / CPU_HZ, reg, cpu.ppc());
	switch (reg & 7) {
	case 0: return m_io.csr;
	case 1: return m_io.ctr;
	case 2: return m_io.addr;
	case 3: {
		int code = m_io.rx_req ? 0 : m_io.tx_req ? 1 : m_io.dma_req ? 2 : 3;
		return u16((m_io.vec & 0360) | (code << 2));
	}
	case 4: return m_io.rcsr;
	case 5: { u16 v = m_io.rbuf; m_io.rcsr &= ~0201; if (!m_io.rxq.empty()) m_io.rcsr |= 1; m_io.rx_req = false; return v; }
	case 6: {
		bool rfd = !hsi_peer_ready || hsi_peer_ready();
		return rfd ? m_io.xcsr : u16(m_io.xcsr & ~0200);
	}
	default: return 0;
	}
}

void tek8501::io_write(int reg, u16 d)
{
	if (g_iotrace) fprintf(stderr, "[8501 %.3fs io wr %d <- %06o pc=%06o]\n", m_cycles / CPU_HZ, reg, d, cpu.ppc());
	switch (reg & 7) {
	case 0: {
		if (d & 02000) { u16 v = m_io.vec, ctr = m_io.ctr, ad = m_io.addr; io_reset(false); m_io.vec = v; m_io.ctr = ctr; m_io.addr = ad; break; }
		bool go = (d & 1) && !(m_io.csr & 1);
		m_io.csr = u16((m_io.csr & 0174200) | (d & 0000577));
		if (go) { m_io.csr &= ~0174200; }               // GO clears errors and DONE
		if (!(m_io.csr & 0100)) m_io.dma_req = false;
		break;
	}
	case 1: m_io.ctr = d; break;
	case 2: m_io.addr = d; break;
	case 3: m_io.vec = d & 0360; break;
	case 4: {
		bool was = m_io.rcsr & 0100;
		m_io.rcsr = u16((m_io.rcsr & 0200) | (d & 0101));
		if (!was && (m_io.rcsr & 0100) && (m_io.rcsr & 0200)) m_io.rx_req = true;
		if (!(m_io.rcsr & 0100)) m_io.rx_req = false;
		break;
	}
	case 6: {
		bool was = m_io.xcsr & 0100;
		m_io.xcsr = u16((m_io.xcsr & 0200) | (d & 0101));
		if (!was && (m_io.xcsr & 0100) && (m_io.xcsr & 0200)) m_io.tx_req = true;
		if (!(m_io.xcsr & 0100)) m_io.tx_req = false;
		break;
	}
	case 7:
		if (m_io.xcsr & 0200) {
			m_io.xbuf = u8(d); m_io.xcsr &= ~0200; m_io.tx_req = false; m_io.tx_timer = hsi_char_cycles;
		}
		break;
	default: break;
	}
}

void tek8501::io_dma_byte_out()
{
	// fetch the next byte from memory for an OUT transfer
	u32 page = u32((m_io.csr >> 4) & 3) << 16;
	u8 b;
	if (m_io.csr & 4) {             // packed: byte addressing
		u16 w = mem_rd(page | (m_io.addr & 0xfffe));
		bool hi = (m_io.addr & 1) ^ bool(m_io.csr & 010);
		b = hi ? u8(w >> 8) : u8(w);
		m_io.addr++;
	} else {
		b = u8(mem_rd(page | (m_io.addr & 0xfffe)));
		m_io.addr += 2;
	}
	m_io.xbuf = b; m_io.xcsr &= ~0200; m_io.tx_timer = hsi_char_cycles;
}

void tek8501::io_tick(int c)
{
	// CSR<1> IN/OUT: the 8501 OS arms a 9-byte transfer with CSR=007 from a buffer it
	// filled itself and times out waiting for the 8301's answer, so 1 = OUT (from memory).
	const bool dma = m_io.csr & 1, in = !(m_io.csr & 2);
	auto dma_count = [&]() {
		if (++m_io.ctr == 0) {
			m_io.csr = u16((m_io.csr & ~1) | 0200);
			if (m_io.csr & 0100) m_io.dma_req = true;
		}
	};
	// transmitter
	if (m_io.tx_timer > 0 && (m_io.tx_timer -= c) <= 0) {
		m_io.tx_timer = 0;
		if (hsi_tx) hsi_tx(m_io.xbuf);
		m_io.xcsr |= 0200;
		if (m_io.xcsr & 0100 && !(dma && !in)) m_io.tx_req = true;
	}
	const bool rfd = !hsi_peer_ready || hsi_peer_ready();
	if (dma && !in && (m_io.xcsr & 0200) && m_io.tx_timer == 0 && rfd) {
		io_dma_byte_out();
		dma_count();
	}
	// receiver: serial bytes always land in RBUF (DONE, NCTS); an active IN DMA
	// drains RBUF, which -- like a program read of RBUF -- drops NCTS again.
	if (m_io.rx_gap > 0) m_io.rx_gap -= c;
	if (!m_io.rxq.empty() && m_io.rx_gap <= 0 && !(m_io.rcsr & 0200)) {
		u8 b = m_io.rxq.front(); m_io.rxq.pop_front(); m_io.rx_gap = hsi_char_cycles;
		if (g_iotrace) fprintf(stderr, "[8501 %.3fs hsi rx %03o%s ctr=%06o]\n", m_cycles / CPU_HZ, b, (dma && in) ? " (DMA)" : "", m_io.ctr);
		m_io.rbuf = b; m_io.rcsr |= 0201;
		if ((m_io.rcsr & 0100) && !(dma && in)) m_io.rx_req = true;
	}
	if (dma && in && (m_io.rcsr & 0200)) {
		u8 b = u8(m_io.rbuf);
		m_io.rcsr &= ~0200; m_io.rx_req = false;
		if (m_io.rxq.empty()) m_io.rcsr &= ~1;
		u32 page = u32((m_io.csr >> 4) & 3) << 16;
		if (m_io.csr & 4) {
			u32 wa = page | (m_io.addr & 0xfffe);
			u16 w = mem_rd(wa);
			bool hi = (m_io.addr & 1) ^ bool(m_io.csr & 010);
			w = hi ? u16((w & 0x00ff) | (b << 8)) : u16((w & 0xff00) | b);
			mem_wr(wa, w); m_io.addr++;
		} else {
			mem_wr(page | (m_io.addr & 0xfffe), b); m_io.addr += 2;
		}
		dma_count();
	}
}

bool tek8501::flush(std::string &err, bool force)
{
	bool ok = true;
	for (auto &d : m_drive) {
		if (!d.present || !d.writeback || !d.dirty) continue;
		// unless forced, wait until writes have been quiet for ~2 s (a whole file save)
		if (!force && m_cycles - d.last_write < u64(CPU_HZ * 2)) continue;
		std::string e;
		bool r = d.is_imd ? save_imd_8501(d.path, d.data, d.comment, e) : save_raw(d.path, d.data, e);
		if (r) d.dirty = false; else { ok = false; err = e; }
	}
	return ok;
}
