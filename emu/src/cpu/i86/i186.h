// license:BSD-3-Clause
// copyright-holders:Carl
// Standalone port of MAME's Intel 80186/80188 core (src/devices/cpu/i86/i186) for the
// Tektronix 8550 emulator, on top of the standalone i86 port (i86.h). The instruction code
// and the on-chip peripherals (interrupt controller, timers, DMA, chip selects, peripheral
// control block) are MAME's; emu_timer/attotime are replaced by a cycle-driven scheduler in
// run(). Local changes are marked [tek8550]; SOURCE.txt lists them.
#ifndef MAME_CPU_I86_I186_H
#define MAME_CPU_I86_I186_H

#pragma once

#include "i86.h"

// [tek8550] DECLARE_DEVICE_TYPE removed; the AMD Am186EM/Am188EM variants are not ported

class i80186_cpu_device : public i8086_common_cpu_device
{
public:
	// [tek8550] input lines of the standalone port (set_input_line). INT0 is line 0, as
	// INPUT_LINE_INT0 on the 8086; NMI and TEST keep the i86 numbers.
	enum {
		I80186_INT0 = 0, I80186_INT1, I80186_INT2, I80186_INT3,
		I80186_TMRIN0, I80186_TMRIN1, I80186_DRQ0, I80186_DRQ1,
		I80186_TEST = INPUT_LINE_TEST, I80186_NMI = INPUT_LINE_NMI
	};

	// construction/destruction
	i80186_cpu_device(bus_interface &bus);   // [tek8550] was (mconfig, tag, owner, clock)
	bool is_80188() const { return m_spaces[AS_PROGRAM].width == 8; }   // [tek8550]

	// [tek8550] run(cycles) counts CPU cycles (MAME's device clock is twice that: X1/2);
	// the timers count at a quarter of it, as on the chip
	virtual int run(int cycles) override;

	// [tek8550] the devcb binders become setters (all optional)
	void set_read_slave_ack_handler(std::function<u8(int)> f) { m_read_slave_ack_func.f = std::move(f); }   // cascade mode INT0/INT1 vector; default: bus.inta()
	void set_chip_select_handler(std::function<void(int, u16)> f) { m_out_chip_select_func.f = std::move(f); }   // (0 UMCS, 1 LMCS, 2 PACS, 3 MMCS, 4 MPCS; value)
	void set_tmrout0_handler(std::function<void(int)> f) { m_out_tmrout0_func.f = std::move(f); }
	void set_tmrout1_handler(std::function<void(int)> f) { m_out_tmrout1_func.f = std::move(f); }
	void set_irmx_irq_handler(std::function<void(int)> f) { m_irmx_irq_cb.f = std::move(f); }
	void set_irqa_handler(std::function<void(int)> f) { m_irqa_cb.f = std::move(f); }
	void set_irmx_irq_ack(std::function<int(int)> f) { m_irmx_irq_ack.f = std::move(f); }   // iRMX mode vector; default: bus.inta()

	// [tek8550] peripheral state for front ends and tests
	u16 relocation() const { return m_reloc; }
	u16 timer_count(int which) { internal_timer_sync(which); return m_timer[which].count; }
	u16 timer_control(int which) const { return m_timer[which].control; }

	void drq0_w(int state) { m_dma[0].drq_state = state; }
	void drq1_w(int state) { m_dma[1].drq_state = state; }
	void tmrin0_w(int state) { external_tmrin(0, state); }
	void tmrin1_w(int state) { external_tmrin(1, state); }
	void int0_w(int state) { external_int(0, state); }
	void int1_w(int state) { external_int(1, state); }
	void int2_w(int state) { external_int(2, state); }
	void int3_w(int state) { external_int(3, state); }

	// This a hack, only use if there are sync problems with another cpu
	void dma_sync_req(int which) { drq_callback(which); }

	// [tek8550] memory_space_config removed

protected:
	// [tek8550] the debugger state enum (I80186_RELREG...) removed

	// [tek8550] stand-ins for MAME's attotime and emu_timer: time is counted in CPU cycles
	// since construction, and timers expire inside run() (see SOURCE.txt)
	struct attotime {
		u64 cycles;
		static const attotime never;
		bool is_never() const { return cycles == ~u64(0); }
	};
	static attotime cycles_to_attotime(u64 c) { return attotime{c}; }
	static u64 attotime_to_cycles(attotime t) { return t.cycles; }
	struct emu_timer {
		i80186_cpu_device *cpu = nullptr;
		int  param = 0;
		bool on = false;
		u64  expire = 0;
		void adjust(attotime t, int p);
		bool enabled() const { return on; }
		attotime remaining() const;
	};
	u64  machine_time() const { return m_in_timer ? m_timer_now : total_cycles(); }
	void timer_adjusted(u64 expire);
	bool fire_timers();
	emu_timer m_emu_timers[3];
	bool m_in_timer = false;
	u64  m_timer_now = 0;

	// [tek8550] devcb adapters not in i86.h
	struct devcb_read8 { std::function<u8(int)> f; u8 operator()(offs_t o) { return f(int(o)); } };
	struct devcb_write16 { std::function<void(int, u16)> f; void operator()(offs_t o, u16 d, u16) { if (f) f(int(o), d); } };
	struct irq_ack_delegate { std::function<int(int)> f; int operator()(i80186_cpu_device &, int line) { return f(line); } };
	using device_t = i80186_cpu_device;

	// [tek8550] the core's own INTR input, which the interrupt controller drives
	// (MAME calls set_input_line(0, ...)); set_input_line now means the chip's pins
	void set_intr_line(int state) { i8086_common_cpu_device::execute_set_input(INPUT_LINE_IRQ0, state); }
	virtual void execute_set_input(int inputnum, int state) override;

	// [tek8550] IRQ_CALLBACK_MEMBERs, protected (they were public)
	int int_callback(device_t &device, int irqline);
	int inta_callback(device_t &device, int irqline);

	i80186_cpu_device(bus_interface &bus, int data_bus_size);   // [tek8550] was (mconfig, type, tag, owner, clock, data_bus_size)

	// device_execute_interface overrides
	// [tek8550] execute_clocks_to_cycles / execute_cycles_to_clocks removed (run() counts CPU cycles)
	virtual void execute_run() override;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual uint8_t fetch() override;
	// [tek8550] update_pc() is in i8086_common_cpu_device

	virtual uint8_t read_port_byte(uint16_t port) override;
	virtual uint16_t read_port_word(uint16_t port) override;
	virtual void write_port_byte(uint16_t port, uint8_t data) override;
	virtual void write_port_byte_al(uint16_t port) override;
	virtual void write_port_word(uint16_t port, uint16_t data) override;
	virtual uint8_t read_byte(uint32_t addr) override;
	virtual uint16_t read_word(uint32_t addr) override;
	virtual void write_byte(uint32_t addr, uint8_t data) override;
	virtual void write_word(uint32_t addr, uint16_t data) override;

	static const uint8_t m_i80186_timing[200];
	static const uint8_t m_i80186_ea_timing[200];

private:
	void update_interrupt_state();
	void handle_eoi(int data);
	void external_int(uint16_t intno, int state);
	void restart_timer(int which);
	void internal_timer_sync(int which);
	void internal_timer_update(int which, int new_count, int new_maxA, int new_maxB, int new_control);
	void external_tmrin(int which, int state);
	void update_dma_control(int which, int new_control);
	void drq_callback(int which);
	void inc_timer(int which);
	uint16_t internal_port_r(offs_t offset, uint16_t mem_mask = ~0);
	void internal_port_w(offs_t offset, uint16_t data);

	void timer_elapsed(int param);   // [tek8550] was TIMER_CALLBACK_MEMBER

	struct mem_state
	{
		uint16_t    lower;
		uint16_t    upper;
		uint16_t    middle;
		uint16_t    middle_size;
		uint16_t    peripheral;
	};

	struct timer_state
	{
		uint16_t    control;
		uint16_t    maxA;
		uint16_t    maxB;
		uint16_t    count;
		emu_timer   *int_timer;
	};

	struct dma_state
	{
		bool        drq_state;
		uint32_t    source;
		uint32_t    dest;
		uint16_t    count;
		uint16_t    control;
	};

	struct intr_state
	{
		uint8_t     vector;
		uint8_t     pending;
		uint16_t    ack_mask;
		uint16_t    priority_mask;
		uint16_t    in_service;
		uint16_t    request;
		uint16_t    status;
		uint16_t    poll_status;
		uint16_t    timer[3];
		uint16_t    dma[2];
		uint16_t    ext[4];
		uint8_t     ext_state;
	};

	timer_state     m_timer[3];
	dma_state       m_dma[2];
	intr_state      m_intr;
	mem_state       m_mem;
	bool            m_last_dma;

	int m_dma_latency;
	uint16_t m_reloc;

	// [tek8550] address_space_config members removed

	devcb_read8 m_read_slave_ack_func;
	devcb_write16 m_out_chip_select_func;
	devcb_write_line m_out_tmrout0_func;
	devcb_write_line m_out_tmrout1_func;
	devcb_write_line m_irmx_irq_cb;
	devcb_write_line m_irqa_cb;
	irq_ack_delegate m_irmx_irq_ack;   // [tek8550] was device_irq_acknowledge_delegate
};

class i80188_cpu_device : public i80186_cpu_device
{
public:
	// construction/destruction
	i80188_cpu_device(bus_interface &bus);   // [tek8550] was (mconfig, tag, owner, clock)
};

// [tek8550] am186em_device / am188em_device not ported

#endif // MAME_CPU_I86_I186_H
