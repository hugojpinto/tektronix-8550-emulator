// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Roberto Fresca, Grull Osgo
// Standalone port of MAME's Intel 8085A core (src/devices/cpu/i8085) for the
// Tektronix 8550 emulator. The MAME device framework is replaced by small adapters
// with the same names, so the instruction code in i8085.cpp is used verbatim.
// Local changes are marked [tek8550].
#ifndef TEK_CPU_I8085_H
#define TEK_CPU_I8085_H
#include "emu.h"
#include <functional>
#include <iterator>

#define I8085_INTR_LINE     0
#define I8085_RST55_LINE    1
#define I8085_RST65_LINE    2
#define I8085_RST75_LINE    3
#define I8085_TRAP_LINE     4          // INPUT_LINE_NMI in MAME

class i8085a_cpu_device
{
public:
	struct bus_interface {
		virtual ~bus_interface() = default;
		virtual u8   read(u16 a) = 0;
		virtual void write(u16 a, u8 d) = 0;
		virtual u8   fetch(u16 a) { return read(a); }  // opcode fetch (S1 = 1, S0 = 1)
		virtual u8   io_read(u8 port) = 0;
		virtual void io_write(u8 port, u8 d) = 0;
		virtual u8   inta() { return 0xff; }          // data bus during INTA (RST 7)
		virtual int  sid() { return 0; }
		virtual void sod(int) {}
	};

	static constexpr u8 STATUS_INTA = 0x01, STATUS_WO = 0x02, STATUS_STACK = 0x04, STATUS_HLTA = 0x08,
	                    STATUS_OUT = 0x10, STATUS_M1 = 0x20, STATUS_INP = 0x40, STATUS_MEMR = 0x80;

	explicit i8085a_cpu_device(bus_interface &bus);
	void reset() { device_reset(); }
	int  run(int cycles) { m_icount = cycles; execute_run(); return cycles - m_icount; }
	void set_input_line(int line, int state) { execute_set_input(line, state); }

	// register access (debugger / firmware emulation)
	u16 pc() const { return m_PC.w.l; }
	u16 sp() const { return m_SP.w.l; }
	u16 af() const { return m_AF.w.l; }
	u16 bc() const { return m_BC.w.l; }
	u16 de() const { return m_DE.w.l; }
	u16 hl() const { return m_HL.w.l; }
	u8  im() const { return m_im; }
	bool halted() const { return m_halt; }
	bool inte() const { return m_im & 0x08; }
	void set_pc(u16 v) { m_PC.w.l = v; m_halt = 0; }
	void set_sp(u16 v) { m_SP.w.l = v; }
	void set_af(u16 v) { m_AF.w.l = v; }
	void set_bc(u16 v) { m_BC.w.l = v; }
	void set_de(u16 v) { m_DE.w.l = v; }
	void set_hl(u16 v) { m_HL.w.l = v; }
	void set_im(u8 v) { m_im = v; }
	std::function<void(u16)> trace_hook;

private:
	// ---- adapters standing in for MAME address spaces / callbacks ----
	struct mem_t  { bus_interface *b; bool op; u8 read_byte(offs_t a) { return op ? b->fetch(u16(a)) : b->read(u16(a)); } void write_byte(offs_t a, u8 d) { b->write(u16(a), d); } };
	struct io_t   { bus_interface *b; u8 read_byte(offs_t a) { return b->io_read(u8(a)); } void write_byte(offs_t a, u8 d) { b->io_write(u8(a), d); } };
	struct line_out { std::function<void(int)> f; void operator()(int s) { if (f) f(s); } };
	struct u8_out   { bool isunset() const { return true; } void operator()(u8) {} };
	struct sid_in   { bus_interface *b; int operator()() { return b->sid(); } };
	struct inta_in  { bus_interface *b; bool isunset() const { return false; } u8 operator()(offs_t) { return b->inta(); } };
	int standard_irq_callback(int, offs_t) { return 0xff; }
	void debugger_instruction_hook(offs_t pc) { if (trace_hook) trace_hook(u16(pc)); }

	bus_interface &m_bus;
	mem_t m_cprogram, m_copcodes, m_program;
	io_t  m_io;
	inta_in  m_in_inta_func;
	u8_out   m_out_status_func;
	line_out m_out_inte_func, m_out_sod_func;
	sid_in   m_in_sid_func;

	void device_start();
	void device_reset();
	void execute_run();
	void execute_set_input(int inputnum, int state);

	PAIR m_PC,m_SP,m_AF,m_BC,m_DE,m_HL,m_WZ;
	u8 m_halt;
	u8 m_im;             // interrupt mask (8085A only)
	u8 m_status;         // status word
	u8 m_after_ei;       // post-EI processing; starts at 2, check for ints at 0
	u8 m_nmi_state;      // raw NMI line state
	u8 m_irq_state[4];   // raw IRQ line states
	bool m_trap_pending; // TRAP interrupt latched?
	u8 m_trap_im_copy;   // copy of IM register when TRAP was taken
	u8 m_sod_state;      // state of the SOD line
	bool m_in_acknowledge;
	u8 m_ietemp;         // import/export temp space

	int m_icount;
	static const u8 lut_cycles_8080[256];
	static const u8 lut_cycles_8085[256];
	u8 lut_cycles[256];
	u8 lut_zs[256];
	u8 lut_zsp[256];

	int ret_taken() { return 6; }
	int jmp_taken() { return 3; }
	int call_taken() { return 9; }
	bool is_8085() { return true; }

	void set_sod(int state);
	void set_inte(int state);
	void set_status(u8 status);
	u8 get_rim_value();
	void break_halt_for_interrupt();
	u8 read_op();
	u8 read_inta();
	u8 read_arg();
	PAIR read_arg16();
	u8 read_mem(u32 a);
	void write_mem(u32 a, u8 v);
	void op_push(PAIR p);
	PAIR op_pop();
	void check_for_interrupts();
	void execute_one(int opcode);
	void init_tables();
	void op_ora(u8 v);
	void op_xra(u8 v);
	void op_ana(u8 v);
	u8 op_inr(u8 v);
	u8 op_dcr(u8 v);
	void op_add(u8 v);
	void op_adc(u8 v);
	void op_sub(u8 v);
	void op_sbb(u8 v);
	void op_cmp(u8 v);
	void op_dad(u16 v);
	void op_jmp(int cond);
	void op_call(int cond);
	void op_ret(int cond);
	void op_rst(u8 v);
};
#endif
