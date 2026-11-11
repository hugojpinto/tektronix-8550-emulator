// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix 8501 Data Management Unit (LSI-11/2 based), part of the 8550 MDL.
// Board structure follows the real unit so it can later become MAME devices:
//   Utility board  : PROMs 160-0399/0400, LTC/LED register, AUX + PRINTER serial ports
//   Memory board   : 32 KW RAM (000000-167776 visible)
//   Flexible Disc Controller : the real board (Z80A + ROM 160-0412-00, tek8501_fdc), or
//                              with fdc_lle = false the old HLE of its command-block interface
//   I/O board      : HSI (high speed serial link to the 8301) -- stub for now
#ifndef TEK_TEK8501_H
#define TEK_TEK8501_H
#include "emu.h"
#include "machine/imd.h"
#include "machine/tek8501_fdc.h"
#include "cpu/t11/t11.h"
#include "machine/serial.h"
#include <vector>
#include <deque>
#include <string>
#include <memory>
#include <functional>

class tek8501 : public t11_device::bus_interface
{
public:
	// MAME T11 cycle units per second, calibrated against the 8501's own timing loops:
	// the disk-based LTC test spins 28100 x (CMP @4(SP),(SP); BLOS; INC @4(SP); JMP)
	// = 108 units/iteration for "one second" and accepts 58..62 ticks; the power-up
	// test wants 776..3213 iterations of TST/BEQ/BMI/SOB per tick (~940 here).
	static constexpr double CPU_HZ = 3'100'000.0;

	tek8501();
	bool load_roms(const std::string &dir, std::string &err);
	// at_run_time: the door was opened and a disc inserted (the drive reports a disc change)
	bool mount(int drive, const std::string &path, std::string &err, bool writeback = false, bool at_run_time = false);
	bool eject(int drive, std::string &err);   // writes the disc back first
	bool mounted(int drive) const { return fdc_lle ? fdc.drv[drive & 3].present : m_drive[drive & 3].present; }
	// write modified, write-back drives to their image files; returns false on error
	bool flush(std::string &err, bool force = false);
	bool dirty(int drive) const { return fdc_lle ? fdc.drv[drive & 3].dirty : m_drive[drive & 3].dirty; }

	// flexible disc controller: low-level (the real board) unless set false before load_roms/mount
	bool fdc_lle = true;
	tek8501_fdc fdc;
	void power_on();
	void restart();          // front-panel RESTART: PC = 173000, LTC<5> = 0
	void run_slice(int cycles);

	t11_device cpu;
	serial_port aux;         // 177560, vector 060 -- console / ODT terminal
	serial_port printer;     // 177510, vector 200

	// LEDs: last value written to LTC<15:11> (1 = off)
	u16 leds() const { return (m_ltc >> 11) & 037; }

	// HSI link to the 8301 (I/O board). Bytes we transmit go to hsi_tx.
	std::function<void(u8)> hsi_tx;
	void hsi_rx(u8 b) { m_io.rxq.push_back(b); m_io.rcsr |= 1; }   // start bit detected -> NCTS
	std::function<bool()> hsi_peer_ready;   // RFD: peer's receiver can take a byte
	bool hsi_ncts() const { return m_io.rcsr & 1; }
	int  hsi_char_cycles = int(CPU_HZ * 11 / 153600);   // 153.6 kbaud, 11 bits/char
	u64 cycles() const { return m_cycles; }
	bool trace = false;
	std::string log_unknown_io;  // collected for diagnostics

	// t11_device::bus_interface
	u16 read16(offs_t addr, bool &nxm) override;
	void write16(offs_t addr, u16 data, bool &nxm) override;
	void write8(offs_t addr, u8 data, bool &nxm) override;
	int iack() override;
	void bus_reset() override;

	// debugger helpers (no side effects)
	u16 peek16(offs_t addr);
	void poke16(offs_t addr, u16 v);

	// ODT (KD11 console microcode) -- active while cpu.halted()
	void odt_char(u8 c);
	void odt_enter();

private:
	std::vector<u16> m_ram;            // words
	u16 m_prom[2][512];                // [LTC<5>][word]
	u16 m_ltc;
	u64 m_cycles = 0;
	u64 m_next_ltc = 0;
	void update_irq();

	// ---- flexible disc controller (HLE) ----
	struct drive_t { std::vector<u8> data; std::string path, comment; bool present = false, changed = true, writeback = false, is_imd = false, dirty = false; u64 last_write = 0; fd_geometry geom; };
	drive_t m_drive[4];
	u16  m_cbpr = 0;
	bool m_fd_busy = false;
	u64  m_fd_done_at = 0;
	bool m_fd_irq = false;
	void fd_start(u16 cbpr);
	void fd_execute();
	u16  mem_rd(u32 a) { return a < m_ram.size() * 2 ? m_ram[a >> 1] : 0; }
	void mem_wr(u32 a, u16 v) { if (a < m_ram.size() * 2) m_ram[a >> 1] = v; }

	// ---- I/O board: HSI serial port with DMA (176510-176526) ----
	struct io_board {
		u16 csr = 0, ctr = 0, addr = 0, vec = 060;
		u16 rcsr = 0, rbuf = 0, xcsr = 0200; u8 xbuf = 0;
		int tx_timer = 0, rx_gap = 0;
		bool rx_req = false, tx_req = false, dma_req = false;
		std::deque<u8> rxq;
	} m_io;
	void io_reset(bool bus);
	u16  io_read(int reg);
	void io_write(int reg, u16 d);
	void io_tick(int c);
	void io_dma_byte_out();
	bool io_irq() const { return m_io.rx_req || m_io.tx_req || m_io.dma_req; }

	// ---- ODT state ----
	std::string m_odt_buf;
	int  m_odt_open = -1;      // open location (address) or -2-n for register n
	void odt_out(const std::string &s) { for (char c : s) aux.host_tx_direct(u8(c)); }
	void odt_show_open();
};
#endif
