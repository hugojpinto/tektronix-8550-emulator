#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
"""Generate emu/src/machine/am9513.{h,cpp} from MAME's am9513 device
(reference/mame/machine/am9513.*). The device code stays verbatim; the MAME
framework is replaced by small adapters: plain class, std::function outputs,
frequency timers ticked by the caller (tick(clocks)), no logging/save state.
usage: python3 -I tools/port_am9513.py   (from emu/)"""
import re
ref = '../reference/mame/machine/'
h = open(ref + 'am9513.h').read()
c = open(ref + 'am9513.cpp').read()

# ---------------- header
h = h.replace('#pragma once\n', '#pragma once\n#include "emu.h"\n#include <functional>\n#include <string>\n')
h = h.replace('class am9513_device : public device_t\n{', '''// [tek8550] standalone port: no MAME device framework (see tools/port_am9513.py)
class am9513_device
{''')
h = re.sub(r'\t// device type constructor\n\tam9513_device\(const machine_config &mconfig, const char \*tag, device_t \*owner, u32 clock = 0\);',
           '\t// [tek8550] construction: clock = X1 frequency (only scales F1-F5); is_am9513a selects the A variant\n\texplicit am9513_device(u32 clock = 0, bool is_am9513a = false);\n\tvoid start() { device_start(); device_clock_changed(); }   // as the MAME framework does at start-up\n\tvoid tick(u32 clocks);                    // [tek8550] advance F1-F5 by X1 clocks', h)
h = re.sub(r'\tauto out(\d)_cb\(\) \{ return m_out_cb\[(\d)\]\.bind\(\); \}', r'\tstd::function<void(int)> &out\1_cb() { return m_out_cb[\2].f; }', h)
h = h.replace('\tauto fout_cb() { return m_fout_cb.bind(); }', '\tstd::function<void(int)> &fout_cb() { return m_fout_cb.f; }')
# [tek8550] read-only view of a counter's mode register, so a board can model what is
# wired to the pins (e.g. pull-ups on outputs left high-impedance)
h = h.replace('\t// diagnostic helper\n', '\t// [tek8550] counter mode register, read only\n\tu16 counter_mode(int c) const { return m_counter_mode[c]; }\n\n\t// diagnostic helper\n')
h = re.sub(r'\t// base constructor\n\tam9513_device\(const machine_config &mconfig, device_type type, const char \*tag, device_t \*owner, u32 clock, bool is_am9513a\);\n', '', h)
h = h.replace('\tvirtual void device_start() override ATTR_COLD;\n\tvirtual void device_clock_changed() override;', '\tvoid device_start();\n\tvoid device_clock_changed();\n\tu32 clock() const { return m_clock; }')
h = h.replace('\tTIMER_CALLBACK_MEMBER(clear_outputs);\n\tTIMER_CALLBACK_MEMBER(timer_tick);', '\tvoid clear_outputs(s32 param);\n\tvoid timer_tick(s32 param);')
h = h.replace('\tdevcb_write_line::array<5> m_out_cb;\n\tdevcb_write_line m_fout_cb;', '''\tstruct line_cb { std::function<void(int)> f; void operator()(int s) { if (f) f(s); } bool isunset() const { return !f; } };
	struct freq_timer { u32 period = 0, acc = 0; s32 param = 0; bool enabled = false;
		void adjust(u32 first, s32 p, u32 per) { (void)first; param = p; period = per; acc = 0; }
		void enable(bool e) { enabled = e; } };
	line_cb m_out_cb[5];
	line_cb m_fout_cb;
	u32 m_clock;''')
h = h.replace('\temu_timer *m_freq_timer[5];', '\tfreq_timer m_freq_timer_obj[5];\n\tfreq_timer *m_freq_timer[5];')
h = re.sub(r'// ======================> am9513a_device.*?DECLARE_DEVICE_TYPE\(AM9513A, am9513a_device\)\n', '', h, flags=re.S)
# ---------------- source
c = c.replace('#include "emu.h"\n#include "am9513.h"', '#include "machine/am9513.h"\n#include <algorithm>\n#include <iterator>\n#include <cstdio>')
c = c.replace('#include "logmacro.h"', '#define LOG(...) do {} while (0)\n#define LOGMASKED(...) do {} while (0)\n#define logerror(...) do {} while (0)\nusing util::string_format;')
c = c.replace('\tif (machine().side_effects_disabled())\n\t\treturn;\n', '')
c = re.sub(r'DEFINE_DEVICE_TYPE\(AM9513A?, .*?\n', '', c)
c = re.sub(r'am9513_device::am9513_device\(const machine_config &mconfig, device_type type.*?\n\{\n\}\n\nam9513_device::am9513_device\(const machine_config &mconfig, const char \*tag.*?\n\{\n\}\n\nam9513a_device::am9513a_device.*?\n\{\n\}\n',
           '''am9513_device::am9513_device(u32 clock, bool is_am9513a)
	: m_clock(clock), m_is_am9513a(is_am9513a)
{
	for (int f = 0; f < 5; f++) m_freq_timer[f] = &m_freq_timer_obj[f];
}

// [tek8550] advance the F1-F5 frequency timers by X1 clocks (timers count half clocks)
void am9513_device::tick(u32 clocks)
{
	for (int f = 0; f < 5; f++) {
		freq_timer &t = m_freq_timer_obj[f];
		if (!t.enabled || t.period == 0) continue;
		t.acc += 2 * clocks;                // periods are in half X1 clocks
		while (t.acc >= t.period) { t.acc -= t.period; timer_tick(t.param); }
	}
}
''', c, flags=re.S)
c = c.replace('\t\tm_freq_timer[f] = timer_alloc(FUNC(am9513_device::timer_tick), this);\n', '')
c = re.sub(r'\tsave_item\(NAME\(\w+\)\);\n', '', c)
# [tek8550] level gating follows the GATE pins as soon as a counter's mode changes: on the
# chip it is combinational, while MAME re-evaluates it only on pin changes, so a counter
# reprogrammed from active-high to active-low gating kept counting (TTA.TST module 46).
c = c.replace("""			break;
		}
	}

	m_counter_mode[c] = data;
}""", """			break;
		}
	}

	m_counter_mode[c] = data;

	// [tek8550] level-gated modes take the current GATE level at once (see tools/port_am9513.py)
	switch ((data >> 13) & 7)
	{
	case 1: m_gate_active[c] = m_tc[(c + 4) % 5] && (bus_is_16_bit() || m_gate_alt[c]); break;
	case 2: m_gate_active[c] = m_gate[(c + 1) % 5] && (bus_is_16_bit() || m_gate_alt[c]); break;
	case 3: m_gate_active[c] = m_gate[(c + 4) % 5] && (bus_is_16_bit() || m_gate_alt[c]); break;
	case 4: m_gate_active[c] = m_gate[c] && (bus_is_16_bit() || m_gate_alt[c]); break;
	case 5: m_gate_active[c] = !(m_gate[c] && (bus_is_16_bit() || m_gate_alt[c])); break;
	default: break;
	}
}""", 1)
c = c.replace('\tmachine().scheduler().synchronize(timer_expired_delegate(FUNC(am9513_device::clear_outputs), this));', '\tclear_outputs(0);')
c = c.replace('TIMER_CALLBACK_MEMBER(am9513_device::clear_outputs)', 'void am9513_device::clear_outputs(s32 param)')
c = c.replace('TIMER_CALLBACK_MEMBER(am9513_device::timer_tick)', 'void am9513_device::timer_tick(s32 param)')
c = c.replace('\tattotime freq = clocks_to_attotime(scale);', '\tu32 freq = u32(scale) * 2;   // [tek8550] in half X1 clocks: full-cycle F1 needs half periods')
open('src/machine/am9513.h', 'w').write(h)
open('src/machine/am9513.cpp', 'w').write(c)
print('ok')
