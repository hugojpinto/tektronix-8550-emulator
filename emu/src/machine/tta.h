// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8550 Trigger Trace Analyzer (TTA) option on the 8301: ports 70-77 and a RAM
// window on the system bus (E800-F9FF). Reverse-engineered from TTA.TST V1.3A
// (8550_DIAG.IMD, Select mode 6); see notes/tta.md. Counters: an AMD Am9513
// System Timing Controller (port of MAME's device) at 76 (data) / 77 (command).
#ifndef TEK_TTA_H
#define TEK_TTA_H
#include "emu.h"
#include "machine/am9513.h"
#include <cstdio>
#include <cstdlib>

class tta
{
public:
	tta() { stc.start(); }
	bool present = false;
	bool trace = getenv("TEK_TTATRACE") != nullptr;
	double *clock = nullptr;

	u8 read(int r);
	void write(int r, u8 d);
	// the RAM window: system-bus addresses E800-F9FF while the RAM is selected
	bool ram_selected() const { return present && (m_reg[0] & 0x02); }
	bool ram_decodes(u16 a) const { return ram_selected() && a >= 0xe800 && a < 0xfa00; }
	u8 ram_read(u16 a);
	void ram_write(u16 a, u8 d);

	am9513_device stc{ 0 };

private:
	u8 m_reg[8] = {};
	u8 m_ram[0x1200] = {};                    // pages E8-F9
	void log(const char *w, int r, u8 v) { if (trace) fprintf(stderr, "[%.4f TTA %s %02x %02x]\n", clock ? *clock : 0.0, w, 0x70 + r, v); }
};
#endif
