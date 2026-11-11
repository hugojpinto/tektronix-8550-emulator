// license:BSD-3-Clause
// copyright-holders:AJR
/**********************************************************************

    Am9513A/Am9513 System Timing Controller

***********************************************************************
                            _____   _____
                   VCC   1 |*    \_/     | 40  OUT 3
                 OUT 2   2 |             | 39  GATE 2
                 OUT 1   3 |             | 38  OUT 4
                GATE 1   4 |             | 37  OUT 5
                    X1   5 |             | 36  GATE 3
                    X2   6 |             | 35  GATE 4
                  FOUT   7 |             | 34  GATE 5
                  C/_D   8 |             | 33  SOURCE 1
                   _WR   9 |             | 32  SOURCE 2
                   _CS  10 |   Am9513A   | 31  SOURCE 3
                   _RD  11 |   Am9513    | 30  SOURCE 4
                   DB0  12 |             | 29  SOURCE 5
                   DB1  13 |             | 28  DB15
                   DB2  14 |             | 27  DB14
                   DB3  15 |             | 26  DB13
                   DB4  16 |             | 25  DB12/GATE 5A
                   DB5  17 |             | 24  DB11/GATE 4A
                   DB6  18 |             | 23  DB10/GATE 3A
                   DB7  19 |             | 22  DB9/GATE 2A
           GATE 1A/DB8  20 |_____________| 21  VSS

**********************************************************************/

#ifndef MAME_MACHINE_AM9513_H
#define MAME_MACHINE_AM9513_H

#pragma once
#include "emu.h"
#include <functional>
#include <string>

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> am9513_device

// [tek8550] standalone port: no MAME device framework (see tools/port_am9513.py)
class am9513_device
{
public:
	// [tek8550] construction: clock = X1 frequency (only scales F1-F5); is_am9513a selects the A variant
	explicit am9513_device(u32 clock = 0, bool is_am9513a = false);
	void start() { device_start(); }
	void tick(u32 clocks);                    // [tek8550] advance F1-F5 by X1 clocks

	// static configuration
	std::function<void(int)> &out1_cb() { return m_out_cb[0].f; }
	std::function<void(int)> &out2_cb() { return m_out_cb[1].f; }
	std::function<void(int)> &out3_cb() { return m_out_cb[2].f; }
	std::function<void(int)> &out4_cb() { return m_out_cb[3].f; }
	std::function<void(int)> &out5_cb() { return m_out_cb[4].f; }
	std::function<void(int)> &fout_cb() { return m_fout_cb.f; }

	// 8-bit data bus interface
	u8 read8(offs_t offset);
	void write8(offs_t offset, u8 data);

	// 16-bit data bus interface
	u16 read16(offs_t offset);
	void write16(offs_t offset, u16 data);

	// Source N inputs
	void source1_w(int state) { write_source(0, state); }
	void source2_w(int state) { write_source(1, state); }
	void source3_w(int state) { write_source(2, state); }
	void source4_w(int state) { write_source(3, state); }
	void source5_w(int state) { write_source(4, state); }

	// Gate N inputs
	void gate1_w(int state) { write_gate(0, state); }
	void gate2_w(int state) { write_gate(1, state); }
	void gate3_w(int state) { write_gate(2, state); }
	void gate4_w(int state) { write_gate(3, state); }
	void gate5_w(int state) { write_gate(4, state); }

	// Gate N alternate inputs (8-bit mode only; multiplexed with DB8-DB12)
	void gate1a_w(int state) { write_gate_alt(0, state); }
	void gate2a_w(int state) { write_gate_alt(1, state); }
	void gate3a_w(int state) { write_gate_alt(2, state); }
	void gate4a_w(int state) { write_gate_alt(3, state); }
	void gate5a_w(int state) { write_gate_alt(4, state); }

	// diagnostic helper
	std::string describe_register() const;

protected:

	// device-level overrides
	void device_start();
	u32 clock() const { return m_clock; }

private:
	// internal helpers
	void clear_outputs(s32 param);
	void timer_tick(s32 param);
	void master_reset();
	void init_freq_timer(int f);
	void select_freq_timer(int f, int c, bool selected, bool cycle);
	void set_master_mode(u16 data);
	bool counter_is_mode_x(int c) const;
	bool compare_count(int c) const;
	void set_counter_mode(int c, u16 data);
	void arm_counter(int c);
	void disarm_counter(int c);
	void save_counter(int c);
	void set_output(int c, bool state);
	void set_toggle(int c, bool state);
	void set_tc(int c, bool state);
	void write_source(int s, bool level);
	void count_edge(int c);
	bool reload_from_hold(int c) const;
	void step_counter(int c, bool force_load);
	void gate_count(int c, bool state);
	void write_gate(int g, bool level);
	void write_gate_alt(int c, bool level);
	u16 internal_read() const;
	void internal_write(u16 data);
	void advance_dpr();
	void command_write(u8 data);
	u8 status_read() const;
	bool bus_is_16_bit() const;
	u16 data_read();
	void data_write(u16 data);
	void fout_tick();

	// output callbacks
	struct line_cb { std::function<void(int)> f; void operator()(int s) { if (f) f(s); } bool isunset() const { return !f; } };
	struct freq_timer { u32 period = 0, acc = 0; s32 param = 0; bool enabled = false;
		void adjust(u32 first, s32 p, u32 per) { (void)first; param = p; period = per; acc = 0; }
		void enable(bool e) { enabled = e; } };
	line_cb m_out_cb[5];
	line_cb m_fout_cb;
	u32 m_clock;

	const bool m_is_am9513a;

	u8 m_dpr;
	u16 m_mmr;
	u8 m_status;
	bool m_write_prefetch;

	// counter-specific registers
	u16 m_count[5];
	u16 m_counter_load[5];
	u16 m_counter_hold[5];
	u16 m_counter_mode[5];
	u16 m_alarm[2];
	bool m_counter_armed[5];
	bool m_counter_running[5];
	bool m_alternate_count[5];

	// input state
	bool m_src[5];
	bool m_gate[5];
	bool m_gate_alt[5];
	bool m_gate_active[5];

	// internal outputs
	bool m_tc[5];
	bool m_toggle[5];

	// frequency timer
	u8 m_f;
	freq_timer m_freq_timer_obj[5];
	freq_timer *m_freq_timer[5];
	u8 m_freq_timer_selected[5];
	u8 m_freq_timer_cycle[5];

	bool m_fout;
	int m_fout_counter;
};


#endif // MAME_MACHINE_AM9513_H
