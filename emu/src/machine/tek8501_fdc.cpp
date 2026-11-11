// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix 8501 Flexible Disc Controller -- see tek8501_fdc.h and notes/8501-fdc.md
#include "machine/tek8501_fdc.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace flex;

tek8501_fdc::tek8501_fdc()
{
	memset(m_rom, 0xff, sizeof m_rom);
	memset(m_ram, 0, sizeof m_ram);
	trace = getenv("TEK_FDCTRACE") != nullptr;
	reset();
}

bool tek8501_fdc::load_rom(const std::string &path, std::string &err)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { err = "missing flexible disc controller ROM " + path; return false; }
	size_t n = fread(m_rom, 1, sizeof m_rom, f); fclose(f);
	if (n != sizeof m_rom) { err = path + ": expected 4096 bytes"; return false; }
	return true;
}

void tek8501_fdc::reset()
{
	u64 keep = m_cycles;
	z80_init(&z);
	z.read_byte = &rd; z.write_byte = &wr; z.port_in = &in; z.port_out = &out; z.userdata = this;
	z.cyc = keep; m_cycles = keep; m_stall_to = 0;
	m_busy = m_multint = m_qirq = false;
	m_hcr0 = m_hcr1 = 0;
	m_crc_err = m_dma_timeout = m_no_data = m_not_ready_rw = false;
	m_rd_armed = false; m_rd_next = -1; m_wr_active = false;
	for (auto &d : drv) d.head_loaded = false;
}

int tek8501_fdc::selected() const
{
	for (int d = 0; d < 4; d++) if (m_hcr0 & (0x10 << d)) return d;
	return -1;
}

drive *tek8501_fdc::sel() { int d = selected(); return d < 0 ? nullptr : &drv[d]; }

track *tek8501_fdc::cur_track()
{
	drive *d = sel();
	if (!d || !d->ready() || d->tracks.empty()) return nullptr;
	return &d->trk((m_hcr0 >> 3) & 1);
}

// --------------------------------------------------------------------------
// LSI-11 side

void tek8501_fdc::cbpr_write(u16 v)
{
	m_cbpr = v;
	if (m_busy) m_multint = true;    // command while busy
	m_busy = true;
	z80_gen_nmi(&z);
	if (trace && dma_read) {
		u16 w[8]; for (int i = 0; i < 8; i++) { w[i] = 0; dma_read(v + 2 * i, w[i]); }
		fprintf(stderr, "[fdc %.3fs cmd %02o drv %d cr %06o wc %06o da %06o dma %06o @%06o]\n", double(m_cycles) / CLOCK, w[0] & 077, (w[0] >> 10) & 3, w[0], w[1], w[2], w[3], v);
	}
}

void tek8501_fdc::dma_start()
{
	u32 a = u32(m_dmaar) | (u32(m_hcr1 & 0x60) << 11);   // HCR1 EA16/EA17
	bool ok;
	if (m_hcr1 & 0x08) ok = dma_write && dma_write(a, m_dtr);   // DMA write (to memory)
	else { u16 v = 0; ok = dma_read && dma_read(a, v); m_drr = v; }
	if (!ok) { m_dma_timeout = true; z.int_pending = true; }   // no response within 16 us
}

// --------------------------------------------------------------------------
// drive interface

u8 tek8501_fdc::hsr0()
{
	u8 v = (m_multint ? 0x02 : 0) | (m_crc_err ? 0x01 : 0);
	if (drive *d = sel()) {
		if (d->cyl == 0) v |= 0x80;
		if (d->ready()) v |= 0x40;
		if (d->index(now())) v |= 0x20;
		if (d->disc_change) v |= 0x10;
		if (d->present && d->two_sided) v |= 0x08;
		if (d->write_protect) v |= 0x04;
	}
	return v;
}

void tek8501_fdc::hcr0_write(u8 v)
{
	u8 old = m_hcr0;
	m_hcr0 = v;
	for (int d = 0; d < 4; d++) {
		if ((old & (0x10 << d)) && !(v & (0x10 << d))) drv[d].disc_change = false;   // deselected: change acknowledged
		drv[d].head_loaded = (v & (0x10 << d)) && (v & 0x04);
	}
	if ((v & 1) && !(old & 1))
		if (drive *d = sel()) d->step(v & 0x02);      // step pulse; direction 1 = in
}

// C000 read: restart the read/write logic (sync search, CRC, no-data timer)
void tek8501_fdc::arm()
{
	wr_settle();
	m_wr_active = false;
	m_rd_armed = true; m_rd_arm_time = now(); m_rd_next = -1;
	m_crc_err = false; m_no_data = false; m_not_ready_rw = false;
}

// E102: wait for the next sync mark (MFM A1, FM C7-clock mark) and return its byte.
// No sync before the second index pulse after arming -> No Data Detected (0.33 s).
u8 tek8501_fdc::read_sync()
{
	track *t = cur_track();
	const u64 c = cc(), n0 = now();
	if (!t) { m_not_ready_rw = true; z.int_pending = true; return 0xff; }
	const u64 deadline = (m_rd_arm_time / ROT + 2) * ROT;
	if (t->formatted() && t->mfm == dd()) {
		const u8 want = dd() ? M_MFM_A1 : M_FM_C7;
		const u32 len = u32(t->data.size());
		for (u64 abs = n0 / c + 1, end = abs + len; abs < end; abs++) {
			if ((abs + 1) * c > deadline) break;
			if (t->mark[abs % len] == want) {
				stall((abs + 1) * c);
				m_rd_next = s64(abs + 1);
				u8 b = t->data[abs % len];
				m_rd_crc = crc16(0xffff, b);
				return b;
			}
		}
	}
	stall(std::max(deadline, n0));
	m_no_data = true; z.int_pending = true;
	return 0xff;
}

// E002: the next byte after the sync; a late reader loses bytes (overrun)
u8 tek8501_fdc::read_next(bool with_wait)
{
	track *t = cur_track();
	const u64 c = cc(), n0 = now();
	if (!t) { m_not_ready_rw = true; z.int_pending = true; return 0xff; }
	u64 abs = m_rd_next < 0 ? n0 / c : u64(m_rd_next);
	if (n0 >= (abs + 2) * c) abs = n0 / c - 1;                       // overrun
	if (with_wait) stall((abs + 1) * c);
	m_rd_next = s64(abs + 1);
	u8 b = 0;
	if (t->formatted() && t->mfm == dd()) b = t->data[abs % t->data.size()];
	m_rd_crc = crc16(m_rd_crc, b);
	return b;
}

// a DWDR write at address a: its control bits apply to the byte already in the
// shift register (see notes: A11/A10/A9 FM clock suppression, A11 MFM C2, A6 MFM
// A1, A8 CRC preset, A7 send CRC)
void tek8501_fdc::dwdr_write(u16 a, u8 v)
{
	wr_settle();
	const u64 c = cc();
	if (!m_wr_active) {
		m_wr_active = true; m_rd_armed = false;
		m_wr_abs = now() / c + 1;
		m_wr_byte = v; m_wr_crc_phase = 0;
		return;
	}
	stall(m_wr_abs * c);                  // wait for the holding register
	wr_commit(a);
	m_wr_abs++;
	m_wr_byte = v;
}

void tek8501_fdc::wr_commit(u16 ctl)
{
	drive *d = sel();
	track *t = cur_track();
	if (!t) { m_not_ready_rw = true; z.int_pending = true; return; }
	if (!t->formatted() || t->mfm != dd()) t->erase(dd());
	u8 b = m_wr_byte;
	if (ctl & 0x0100) m_wr_crc = 0xffff;
	if (ctl & 0x0080) { b = m_wr_crc_phase == 0 ? u8(m_wr_crc >> 8) : u8(m_wr_crc); m_wr_crc_phase ^= 1; }
	else { m_wr_crc_phase = 0; m_wr_crc = crc16(m_wr_crc, b); }
	u8 m = M_NONE;
	if (dd()) m = (ctl & 0x0040) ? M_MFM_A1 : (ctl & 0x0800) ? M_MFM_C2 : M_NONE;
	else switch ((ctl >> 9) & 7) {       // A11 A10 A9
		case 0: break;
		case 7: m = M_FM_C7; break;
		case 5: m = M_FM_D7; break;
		default: m = M_FM_OTHER; break;
	}
	size_t i = m_wr_abs % t->data.size();
	t->data[i] = b; t->mark[i] = m;
	d->dirty = true; d->last_write = z.cyc;
}

// the last byte written runs out of the shift register: write gate off
void tek8501_fdc::wr_settle()
{
	if (m_wr_active && now() >= (m_wr_abs + 1) * cc()) { wr_commit(0); m_wr_active = false; }
}

// --------------------------------------------------------------------------
// Z80 bus

u8 tek8501_fdc::mem_read(u16 a)
{
	switch (a >> 13) {
	case 0: return m_rom[a & 0x0fff];
	case 1: return m_ram[a & 0x03ff];
	case 4: return hsr0();                                  // 8000 HSR0
	case 5: return hsr1();                                  // A000 HSR1
	case 6: arm(); return 0xff;                             // C000 arm read/write logic
	case 7:
		switch ((a >> 1) & 7) {
		case 1:                                             // E002 DRDR
			if (a & 0x0100) return read_sync();
			if (a & 0x0400) { m_crc_err = m_rd_crc != 0; m_rd_armed = false; m_rd_next = -1; return 0xff; }
			return read_next(true);
		case 4: return (a & 1) ? u8(m_drr >> 8) : u8(m_drr);         // E008 DRR
		case 5: return (a & 1) ? u8(m_cbpr >> 8) : u8(m_cbpr);       // E00A CBPR
		case 6:                                                      // E00C end of command
			m_busy = false;
			if (trace) {
				auto w = [&](int o) { return unsigned(m_ram[o] | (m_ram[o + 1] << 8)); };
				fprintf(stderr, "[fdc %.3fs done: wc %06o da %06o dma %06o er %06o sr %06o dr %06o]\n", double(z.cyc) / CLOCK, w(4), w(6), w(8), w(10), w(12), w(14));
			}
			return 0xff;
		default: return 0xff;
		}
	default: return 0xff;
	}
}

void tek8501_fdc::mem_write(u16 a, u8 v)
{
	switch (a >> 13) {
	case 1: m_ram[a & 0x03ff] = v; break;
	case 2: hcr0_write(v); break;                           // 4000 HCR0
	case 3: m_hcr1 = v; break;                              // 6000 HCR1
	case 7:
		switch ((a >> 1) & 3) {
		case 0: dwdr_write(a, v); break;                    // E000 DWDR (+ control bits)
		case 2:                                             // E004 DTR; with A8, interrupt
			m_dtr = (a & 1) ? u16((m_dtr & 0x00ff) | (v << 8)) : u16((m_dtr & 0xff00) | v);
			if ((a & 0x0101) == 0x0101) m_qirq = true;
			break;
		case 3:                                             // E006 DMAAR; high byte starts DMA
			m_dmaar = (a & 1) ? u16((m_dmaar & 0x00ff) | (v << 8)) : u16((m_dmaar & 0xff00) | v);
			if (a & 1) dma_start();
			break;
		default: break;
		}
		break;
	default: break;
	}
}

u8 tek8501_fdc::rd(void *p, u16 a) { return static_cast<tek8501_fdc *>(p)->mem_read(a); }
void tek8501_fdc::wr(void *p, u16 a, u8 v) { static_cast<tek8501_fdc *>(p)->mem_write(a, v); }

// IN (00): clear the status flip-flops and the interrupt
u8 tek8501_fdc::in(z80 *zz, u16)
{
	tek8501_fdc *f = static_cast<tek8501_fdc *>(zz->userdata);
	f->m_dma_timeout = f->m_no_data = f->m_not_ready_rw = false;
	f->m_multint = false;
	zz->int_pending = false;
	return 0xff;
}
void tek8501_fdc::out(z80 *, u16, u8) {}

void tek8501_fdc::run_until(u64 t)
{
	while (z.cyc < t) {
		if (z.halted && !z.nmi_pending && !(z.iff1 && int_line())) { z.cyc = t; break; }
		// the ROM's 1 ms delay loop (0267: DEC C / JR NZ at 026A): skip whole iterations
		if (z.pc == 0x026a && !z.nmi_pending && z.c > 1) {
			u64 k = std::min<u64>(z.c - 1, (t - z.cyc) / 16);
			if (k) { z.c = u8(z.c - k); z.cyc += 16 * k; z.r = u8((z.r & 0x80) | ((z.r + 2 * k) & 0x7f)); continue; }
		}
		z.int_pending = int_line();
		m_stall_to = 0;
		z80_step(&z);
		if (m_stall_to > z.cyc) z.cyc = m_stall_to;
		m_stall_to = 0;
	}
	m_cycles = z.cyc;
	wr_settle();
}
