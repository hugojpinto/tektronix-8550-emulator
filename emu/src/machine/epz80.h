// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Z80 emulator processor board (8300E04): a Z80A run by the Emulator Controller as its slave,
// with control port E3 (bit 7 enable, bits 6-5 emulation mode, bit 1 = 0 maps the firmware
// PROM: 80 firmware call / 92 run in mode 0, C0 / B2 / F2 in modes 1-2 as DOS/50 V1.0
// drives it), a firmware PROM (not dumped; high level here) at F800-F9FF, an NMI as the
// forced interrupt and the prototype's pins in modes 1/2. Reverse-engineered from the Z80
// diagnostic and DOS/50 (notes/8301.md).
#ifndef TEK_MACHINE_EPZ80_H
#define TEK_MACHINE_EPZ80_H
#include "machine/z80_slave.h"
#include "machine/emulator_specifics.h"
#include <memory>
#include <vector>

class epz80_board : public z80_slave
{
public:
	explicit epz80_board(tek8301 &m);
	bool present = true;                       // fitted (the default board); -ep selects another
	u8 control = 0;                            // E3 as written
	std::vector<u8> prom;                      // optional firmware PROM dump (512 bytes, -ep-prom)
	bool decodes(u8 port) const override { return port == 0xe3; }
	u8 io_read(u8 port) override;
	void io_write(u8 port, u8 d) override;
	void restart() override { control = 0; }

	const char *name() const override { return "EP"; }
	bool enabled() const override { return present && (control & 0x80); }
	double clock_hz() const override { return 4e6; }
	const emulator_specifics *specifics() const override { return m_spec.get(); }
	bool tta_visible() const override { return true; }
	bool forced_jump(u16 target) override;
	bool forced_interrupt() override;
	void pre_step() override;
	void post_step() override;
	bool prom_window(u16 a, u8 &v) const override;
	// E3 bit 6 or 5 selects the prototype (modes 1/2); both: all memory is the prototype's (mode 2)
	bool prototype_mode() const override { return enabled() && (control & 0x60); }
	bool prototype_all() const override { return enabled() && (control & 0x60) == 0x60; }
protected:
	bool prototype_io() const override { return prototype_mode(); }
	bool svc_stops() const override { return true; }
private:
	std::unique_ptr<emulator_specifics> m_spec;
	bool m_trst_prev = false;                  // v3 target RESET edge
	bool m_nmi_prev = false;
	void firmware(u16 entry);
	bool firmware_mapped() const;              // E3<7> = 1, E3<1> = 0: the PROM answers at F800-F9FF
};
#endif
