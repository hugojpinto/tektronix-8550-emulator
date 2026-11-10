// Tektronix 8501 Data Management Unit (LSI-11/2 based), part of the 8550 MDL.
// Board structure follows the real unit so it can later become MAME devices:
//   Utility board  : PROMs 160-0399/0400, LTC/LED register, AUX + PRINTER serial ports
//   Memory board   : 32 KW RAM (000000-167776 visible)
//   Flexible Disc Controller : HLE of the Z80A controller's command-block interface
//   I/O board      : HSI (high speed serial link to the 8301) -- stub for now
#ifndef TEK_TEK8501_H
#define TEK_TEK8501_H
#include "emu.h"
#include "cpu/t11/t11.h"
#include "machine/serial.h"
#include <vector>
#include <deque>
#include <string>
#include <memory>

class tek8501 : public t11_device::bus_interface
{
public:
	// MAME T11 cycle units per second. Calibrated so the diag LTC test loop runs
	// ~1390 iterations per 60 Hz tick, like a real LSI-11/2 (window 776..3213).
	static constexpr double CPU_HZ = 4'500'000.0;

	tek8501();
	bool load_roms(const std::string &dir, std::string &err);
	bool mount(int drive, const std::string &path, std::string &err);
	void power_on();
	void restart();          // front-panel RESTART: PC = 173000, LTC<5> = 0
	void run_slice(int cycles);

	t11_device cpu;
	serial_port aux;         // 177560, vector 060 -- console / ODT terminal
	serial_port printer;     // 177510, vector 200

	// LEDs: last value written to LTC<15:11> (1 = off)
	u16 leds() const { return (m_ltc >> 11) & 037; }
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
	struct drive_t { std::vector<u8> data; std::string path; bool present = false; bool changed = true; };
	drive_t m_drive[4];
	u16  m_cbpr = 0;
	bool m_fd_busy = false;
	u64  m_fd_done_at = 0;
	bool m_fd_irq = false;
	void fd_start(u16 cbpr);
	void fd_execute();
	u16  mem_rd(u32 a) { return a < m_ram.size() * 2 ? m_ram[a >> 1] : 0; }
	void mem_wr(u32 a, u16 v) { if (a < m_ram.size() * 2) m_ram[a >> 1] = v; }

	// ---- I/O board (HSI) stub ----
	u16 m_hsi[8] = {};

	// ---- ODT state ----
	std::string m_odt_buf;
	int  m_odt_open = -1;      // open location (address) or -2-n for register n
	void odt_out(const std::string &s) { for (char c : s) aux.host_tx_direct(u8(c)); }
	void odt_show_open();
};
#endif
