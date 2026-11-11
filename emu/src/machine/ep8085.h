// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8085 emulator processor board (8300E06): an 8085A run by the Emulator Controller as its
// slave, with control port E5 (80 firmware call, 92 run in mode 0, as DOS/50 V1.0 drives
// it) and a firmware PROM (not dumped; high-level here) at the top of memory. Reverse-
// engineered from 8085.TST V1.1 and DOS/50 (notes/8301.md).
#ifndef TEK_MACHINE_EP8085_H
#define TEK_MACHINE_EP8085_H
#include "machine/emulator_board.h"
#include "machine/emulator_specifics.h"
#include "cpu/i8085/i8085.h"
#include <memory>

class ep8085_board : public emulator_board
{
public:
	explicit ep8085_board(tek8301 &m);
	bool present = false;
	u8 control = 0;                            // E5 as written
	bool decodes(u8 port) const override { return port == 0xe5; }
	// E5 read: 10 halted; 01 the SOD pin (DOS/50's 8085 support stores E5 in block byte 13
	// after each dump and DS shows its bit 0 as SOD, its bits 7/6 as I6.5/I5.5: the RST
	// 6.5/5.5 inputs, which only a prototype drives)
	u8 io_read(u8) override { return present ? u8(((control & 0x80) && cpu.halted() ? 0x10 : 0x00) | (m_sod & 1)) : 0x00; }
	void io_write(u8 port, u8 d) override;
	void restart() override { control = 0; }

	const char *name() const override { return "8085"; }
	bool enabled() const override { return present && (control & 0x80); }
	// X1 = 6.25 MHz in mode 0 (Tektronix catalogue 1982, 8085A emulator support package: "Mode 0
	// (system clock) 6.25 MHz +-0.01%"); the 8085 divides X1 by two
	double clock_hz() const override { return 3.125e6; }
	const emulator_specifics *specifics() const override { return m_spec.get(); }
	bool rtpa_visible() const override { return false; }
	bool tta_visible() const override { return true; }
	void forced_reset() override { cpu.reset(); }
	bool forced_jump(u16 target) override;
	bool forced_interrupt() override;
	void step() override;
	u16 pc() const override { return cpu.pc(); }
	bool halted() const override { return cpu.halted(); }
	u64 cycles() const override { return m_t; }
	bool prom_window(u16 a, u8 &v) const override;

	i8085a_cpu_device &processor() { return cpu; }
private:
	struct bus_t : i8085a_cpu_device::bus_interface {
		ep8085_board *b = nullptr;
		u8 read(u16 a) override;
		void write(u16 a, u8 d) override;
		u8 fetch(u16 a) override;
		u8 io_read(u8 port) override;
		void io_write(u8 port, u8 d) override;
		u8 inta() override { return 0xff; }
		void sod(int s) override { if (b) b->m_sod = u8(s); }   // b is set after the core's reset in its constructor
	} m_bus;
	i8085a_cpu_device cpu{m_bus};
	std::unique_ptr<emulator_specifics> m_spec;
	u64 m_t = 0;                               // 8085 clock states run
	u16 m_resume = 0;                          // user PC the firmware returns to after a restore
	bool m_trap = false;                       // TRAP asserted until an instruction has run
	u8 m_sod = 0;                              // the 8085's SOD output, read back at E5 bit 0
	unsigned m_steps = 0;                      // trace limiter
	void firmware(u16 entry);
};
#endif
