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
	u8 control_read() const { return present ? u8((control & 0x80) && cpu.halted() ? 0x10 : 0x00) : 0x00; }

	const char *name() const override { return "8085"; }
	bool enabled() const override { return present && (control & 0x80); }
	double clock_hz() const override { return 3e6; }
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
	} m_bus;
	i8085a_cpu_device cpu{m_bus};
	std::unique_ptr<emulator_specifics> m_spec;
	u64 m_t = 0;                               // 8085 clock states run
	u16 m_resume = 0;                          // user PC the firmware returns to after a restore
	bool m_trap = false;                       // TRAP asserted until an instruction has run
	unsigned m_steps = 0;                      // trace limiter
	void firmware(u16 entry);
};
#endif
