// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
// Standalone port of MAME's Signetics 2650 core (src/devices/cpu/s2650) for the
// Tektronix 8550 emulator. The MAME device framework is replaced by small
// adapters with the same names, so s2650.cpp is used almost verbatim.
#ifndef TEK_CPU_S2650_H
#define TEK_CPU_S2650_H
#include "emu.h"
#include "cpu/s2650/2650dasm.h"
#include <cstring>

enum { S2650_CTRL_PORT = 0, S2650_DATA_PORT = 1 };

class s2650_device
{
public:
	struct bus_interface {
		virtual ~bus_interface() = default;
		virtual u8   read(offs_t a) = 0;               // 15-bit program space
		virtual void write(offs_t a, u8 d) = 0;
		virtual u8   io_read(offs_t port) = 0;         // 0x00-0xff extended; 0x100/0x101 = CTRL/DATA (REDC/REDD)
		virtual void io_write(offs_t port, u8 d) = 0;
		virtual int  intack() = 0;                     // vector byte for ZBSR (bit 7 = indirect)
		virtual int  sense() { return 0; }
		virtual void flag(int) {}
	};

	explicit s2650_device(bus_interface &bus);
	void reset();
	int  run(int cycles);
	void set_irq(int state) { m_irq_state = u8(state); }

	u16 pc() const { return u16(m_page + m_iar); }
	u16 ppc() const { return m_ppc; }
	u8  reg(int n) const { return m_reg[n]; }
	u8  psl() const { return m_psl; }
	u8  psu() const { return m_psu; }
	bool halted() const { return m_halt; }
	u64 insn_count() const { return m_insns; }
	int icount() const { return m_icount; }   // [tek8550] clocks left in run(): exact device timing
	std::function<void(u16)> trace_hook;
	// [tek8550] machine hook before each instruction (software compatibility fixes),
	// with access to the current register bank (MAME swaps banks on RS changes, so
	// m_reg[1..3] always hold the current one) and the top of the return address stack
	std::function<void(u16)> exec_hook;
	void set_reg_bank(int n, u8 v) { m_reg[n] = v; }
	u16 ras_top() const { return m_ras[m_psu & 7]; }   // push = SP + 1, then store

private:
	// ---- adapters standing in for MAME address spaces / callbacks ----
	struct prog_t { bus_interface *b; u8 read_byte(offs_t a) { return b->read(a & 0x7fff); } void write_byte(offs_t a, u8 d) { b->write(a & 0x7fff, d); } };
	struct data_t { bus_interface *b; u8 read_byte(offs_t a) { return b->io_read(0x100 | (a & 1)); } void write_byte(offs_t a, u8 d) { b->io_write(0x100 | (a & 1), d); } };
	struct io_t   { bus_interface *b; u8 read_byte(offs_t a) { return b->io_read(a & 0xff); } void write_byte(offs_t a, u8 d) { b->io_write(a & 0xff, d); } };
	struct sense_t  { bus_interface *b; bool isunset() const { return false; } int operator()() { return b->sense(); } };
	struct flag_t   { bus_interface *b; void operator()(int s) { b->flag(s); } };
	struct intack_t { bus_interface *b; int operator()() { return b->intack(); } };
	void standard_irq_callback(int, offs_t) {}
	const char *tag() const { return "s2650"; }
	template <typename... A> void logerror(const char *f, A... a) { fprintf(stderr, f, a...); }
	void debugger_instruction_hook(offs_t pc) { if (exec_hook) exec_hook(u16(pc)); if (trace_hook) trace_hook(u16(pc)); }

	bus_interface &m_bus;
	prog_t m_program, m_cprogram;
	data_t m_data;
	io_t   m_io;
	sense_t m_sense_handler;
	flag_t m_flag_handler;
	intack_t m_intack_handler;

	uint16_t  m_ppc;    // previous program counter (page + iar)
	uint16_t  m_page;   // 8K page select register (A14..A13)
	uint16_t  m_iar;    // instruction address register (A12..A0)
	uint16_t  m_ea;     // effective address (A14..A0)
	uint8_t   m_psl;    // processor status lower
	uint8_t   m_psu;    // processor status upper
	uint8_t   m_r;      // absolute addressing dst/src register
	uint8_t   m_reg[7]; // 7 general purpose registers
	uint8_t   m_halt;   // 1 if cpu is halted
	uint8_t   m_ir;     // instruction register
	uint16_t  m_ras[8]; // 8 return address stack entries
	uint8_t   m_irq_state;
	int       m_icount;
	u64       m_insns = 0;

	inline void set_psu(uint8_t new_val);
	inline uint8_t get_psu();
	inline uint8_t get_sp();
	inline void set_sp(uint8_t new_sp);
	inline int check_irq_line();
	inline uint8_t ROP();
	inline uint8_t ARG();
	void s2650_set_flag(int state);
	int s2650_get_flag();
	void s2650_set_sense(int state);
};
#endif
