// license:BSD-3-Clause
// copyright-holders:Carl
// Standalone port of MAME's Intel 8086/8088 core (src/devices/cpu/i86) for the
// Tektronix 8550 emulator. The MAME device framework is replaced by small adapters
// with the same names, so the instruction code in i86.cpp and i86inline.h is used
// verbatim. Only the 8086 and 8088 are ported (not the 80186/80286).
// Local changes are marked [tek8550]; SOURCE.txt lists them.
#ifndef MAME_CPU_I86_I86_H
#define MAME_CPU_I86_I86_H

#pragma once

#include "emu.h"                    // [tek8550] was #include "endianness.h" (now in emu.h)
#include "cpu/i386/i386dasm.h"      // [tek8550] was <cpu/i386/i386dasm.h>
#include <functional>
#include <memory>

/////////////////////////////////////////////////////////////////

// [tek8550] DECLARE_DEVICE_TYPE and the debugger state enum removed; MAME's input line numbers
#ifndef INPUT_LINE_IRQ0
#define INPUT_LINE_IRQ0         0
#endif
#ifndef INPUT_LINE_NMI
#define INPUT_LINE_NMI          32
#endif
#define INPUT_LINE_INT0         INPUT_LINE_IRQ0
#define INPUT_LINE_TEST         20


class i8086_common_cpu_device : public i386_disassembler::config   // [tek8550] was also a cpu_device
{
public:
	// [tek8550] the bus the core drives, in place of MAME's address spaces and devcb callbacks.
	// Memory addresses are 20-bit physical addresses; 'space' tells which bus space the
	// access belongs to (what the real chip signals on S3/S4, plus opcode fetches).
	enum { SP_PROGRAM = 0, SP_OPCODES, SP_STACK, SP_CODE, SP_EXTRA };
	struct bus_interface {
		virtual ~bus_interface() = default;
		virtual u8   read(int space, u32 addr) = 0;
		virtual void write(int space, u32 addr, u8 data) = 0;
		virtual u8   io_read(u16 port) = 0;
		virtual void io_write(u16 port, u8 data) = 0;
		virtual u8   inta() { return 0xff; }       // vector byte on interrupt acknowledge (INTR)
		virtual void lock(int state) {}            // the LOCK pin (m_lock_handler)
		virtual void esc_opcode(u32 pc) {}         // ESC (8087) hooks: m_esc_opcode_handler
		virtual void esc_data(u32 ea) {}           //                   m_esc_data_handler
	};

	// [tek8550] public interface of the standalone port
	virtual ~i8086_common_cpu_device() = default;
	void reset() { device_reset(); }
	int  run(int cycles);               // returns the cycles executed (all of them while halted)
	void set_input_line(int line, int state) { execute_set_input(line, state); }
	std::unique_ptr<util::disasm_interface> create_disassembler();

	u16 ax() const { return m_regs.w[AX]; }
	u16 bx() const { return m_regs.w[BX]; }
	u16 cx() const { return m_regs.w[CX]; }
	u16 dx() const { return m_regs.w[DX]; }
	u16 sp() const { return m_regs.w[SP]; }
	u16 bp() const { return m_regs.w[BP]; }
	u16 si() const { return m_regs.w[SI]; }
	u16 di() const { return m_regs.w[DI]; }
	u16 cs() const { return m_sregs[CS]; }
	u16 ds() const { return m_sregs[DS]; }
	u16 es() const { return m_sregs[ES]; }
	u16 ss() const { return m_sregs[SS]; }
	u16 ip() const { return m_ip; }
	u16 flags() const;
	u32 pc() const { return ((m_sregs[CS] << 4) + m_ip) & 0xfffff; }   // CS:IP as a 20-bit address
	bool halted() const { return m_halt; }
	bool locked() const { return m_lock_pin; }   // the LOCK pin: between bus.lock(1) and bus.lock(0)
	// [tek8550] a prefix has run but its instruction has not: no instruction boundary yet
	bool mid_instruction() const { return m_seg_prefix_next || m_lock; }
	// [tek8550] the segment of the data access in progress (ES 0, CS 1, SS 2, DS 3): S4/S3
	int bus_segment() const { return m_easeg; }
	void set_ax(u16 v) { m_regs.w[AX] = v; }
	void set_bx(u16 v) { m_regs.w[BX] = v; }
	void set_cx(u16 v) { m_regs.w[CX] = v; }
	void set_dx(u16 v) { m_regs.w[DX] = v; }
	void set_sp(u16 v) { m_regs.w[SP] = v; }
	void set_bp(u16 v) { m_regs.w[BP] = v; }
	void set_si(u16 v) { m_regs.w[SI] = v; }
	void set_di(u16 v) { m_regs.w[DI] = v; }
	void set_cs(u16 v) { m_sregs[CS] = v; update_pc(); }
	void set_ds(u16 v) { m_sregs[DS] = v; }
	void set_es(u16 v) { m_sregs[ES] = v; }
	void set_ss(u16 v) { m_sregs[SS] = v; }
	void set_ip(u16 v) { m_prev_ip = m_ip = v; update_pc(); m_halt = false; }
	void set_flags(u16 v);
	std::function<void(u32)> trace_hook;   // called before each instruction with CS:IP (20-bit)

protected:
	// ---- [tek8550] adapters standing in for MAME address spaces, callbacks and the execute interface ----
	enum { AS_PROGRAM = SP_PROGRAM, AS_OPCODES = SP_OPCODES, AS_IO = SP_EXTRA + 1 };
	struct address_space {
		bus_interface *b; int sp;
		u8   read_byte(offs_t a) { return sp == AS_IO ? b->io_read(u16(a)) : b->read(sp, a & 0xfffff); }
		void write_byte(offs_t a, u8 d) { if (sp == AS_IO) b->io_write(u16(a), d); else b->write(sp, a & 0xfffff, d); }
		u16  read_word_unaligned(offs_t a) { u16 l = read_byte(a); return l | (read_byte(a + 1) << 8); }
		void write_word_unaligned(offs_t a, u16 d) { write_byte(a, u8(d)); write_byte(a + 1, u8(d >> 8)); }
		void write_word(offs_t a, u16 d, u16 mask) { if (mask & 0x00ff) write_byte(a, u8(d)); if (mask & 0xff00) write_byte(a + 1, u8(d >> 8)); }
	};
	struct devcb_write_line { std::function<void(int)> f; void operator()(int s) { if (f) f(s); } };
	struct devcb_write32    { std::function<void(u32)> f; void operator()(u32 v) { if (f) f(v); } };
	bus_interface &m_bus;
	address_space m_spaces[AS_IO + 1];
	bool has_space(int) const { return true; }
	address_space &space(int n) { return m_spaces[n]; }
	int standard_irq_callback(int, offs_t) { return m_bus.inta(); }
	void debugger_instruction_hook(offs_t pc) { if (trace_hook) trace_hook(pc & 0xfffff); }
	void debugger_wait_hook() {}
	void debugger_exception_hook(int) {}
	bool access_to_be_redone() { return false; }
	template <typename... A> void logerror(const char *, A...) {}
	u64 total_cycles() const { return m_total_cycles + (m_running ? u64(m_run_budget - m_icount) : 0); }
	u64  m_total_cycles = 0;
	int  m_run_budget = 0;
	bool m_running = false;
	virtual void execute_run() = 0;
	uint32_t update_pc() { return m_pc = (m_sregs[CS] << 4) + m_ip; }   // (moved up from i8086_cpu_device)

	enum
	{
		EXCEPTION, IRET,                                /* EXCEPTION, iret */
		INT3, INT_IMM, INTO_NT, INTO_T,                 /* intS */
		OVERRIDE,                                       /* SEGMENT OVERRIDES */
		FLAG_OPS, LAHF, SAHF,                           /* FLAG OPERATIONS */
		AAA, AAS, AAM, AAD,                             /* ARITHMETIC ADJUSTS */
		DAA, DAS,                                       /* DECIMAL ADJUSTS */
		CBW, CWD,                                       /* SIGN EXTENSION */
		HLT, LOAD_PTR, LEA, NOP, WAIT, XLAT,            /* MISC */

		JMP_SHORT, JMP_NEAR, JMP_FAR,                   /* DIRECT jmpS */
		JMP_R16, JMP_M16, JMP_M32,                      /* INDIRECT jmpS */
		CALL_NEAR, CALL_FAR,                            /* DIRECT callS */
		CALL_R16, CALL_M16, CALL_M32,                   /* INDIRECT callS */
		RET_NEAR, RET_FAR, RET_NEAR_IMM, RET_FAR_IMM,   /* RETURNS */
		JCC_NT, JCC_T, JCXZ_NT, JCXZ_T,                 /* CONDITIONAL jmpS */
		LOOP_NT, LOOP_T, LOOPE_NT, LOOPE_T,             /* LOOPS */

		IN_IMM8, IN_IMM16, IN_DX8, IN_DX16,             /* PORT READS */
		OUT_IMM8, OUT_IMM16, OUT_DX8, OUT_DX16,         /* PORT WRITES */

		MOV_RR8, MOV_RM8, MOV_MR8,                      /* MOVE, 8-BIT */
		MOV_RI8, MOV_MI8,                               /* MOVE, 8-BIT IMMEDIATE */
		MOV_RR16, MOV_RM16, MOV_MR16,                   /* MOVE, 16-BIT */
		MOV_RI16, MOV_MI16,                             /* MOVE, 16-BIT IMMEDIATE */
		MOV_AM8, MOV_AM16, MOV_MA8, MOV_MA16,           /* MOVE, al/ax MEMORY */
		MOV_SR, MOV_SM, MOV_RS, MOV_MS,                 /* MOVE, SEGMENT REGISTERS */
		XCHG_RR8, XCHG_RM8,                             /* EXCHANGE, 8-BIT */
		XCHG_RR16, XCHG_RM16, XCHG_AR16,                /* EXCHANGE, 16-BIT */

		PUSH_R16, PUSH_M16, PUSH_SEG, PUSHF,            /* PUSHES */
		POP_R16, POP_M16, POP_SEG, POPF,                /* POPS */

		ALU_RR8, ALU_RM8, ALU_MR8,                      /* alu OPS, 8-BIT */
		ALU_RI8, ALU_MI8, ALU_MI8_RO,                   /* alu OPS, 8-BIT IMMEDIATE */
		ALU_RR16, ALU_RM16, ALU_MR16,                   /* alu OPS, 16-BIT */
		ALU_RI16, ALU_MI16, ALU_MI16_RO,                /* alu OPS, 16-BIT IMMEDIATE */
		ALU_R16I8, ALU_M16I8, ALU_M16I8_RO,             /* alu OPS, 16-BIT W/8-BIT IMMEDIATE */
		MUL_R8, MUL_R16, MUL_M8, MUL_M16,               /* mul */
		IMUL_R8, IMUL_R16, IMUL_M8, IMUL_M16,           /* imul */
		DIV_R8, DIV_R16, DIV_M8, DIV_M16,               /* div */
		IDIV_R8, IDIV_R16, IDIV_M8, IDIV_M16,           /* idiv */
		INCDEC_R8, INCDEC_R16, INCDEC_M8, INCDEC_M16,   /* inc/dec */
		NEGNOT_R8, NEGNOT_R16, NEGNOT_M8, NEGNOT_M16,   /* neg/not */

		ROT_REG_1, ROT_REG_BASE, ROT_REG_BIT,           /* REG SHIFT/ROTATE */
		ROT_M8_1, ROT_M8_BASE, ROT_M8_BIT,              /* M8 SHIFT/ROTATE */
		ROT_M16_1, ROT_M16_BASE, ROT_M16_BIT,           /* M16 SHIFT/ROTATE */

		CMPS8, REP_CMPS8_BASE, REP_CMPS8_COUNT,         /* cmps 8-BIT */
		CMPS16, REP_CMPS16_BASE, REP_CMPS16_COUNT,      /* cmps 16-BIT */
		SCAS8, REP_SCAS8_BASE, REP_SCAS8_COUNT,         /* scas 8-BIT */
		SCAS16, REP_SCAS16_BASE, REP_SCAS16_COUNT,      /* scas 16-BIT */
		LODS8, REP_LODS8_BASE, REP_LODS8_COUNT,         /* lods 8-BIT */
		LODS16, REP_LODS16_BASE, REP_LODS16_COUNT,      /* lods 16-BIT */
		STOS8, REP_STOS8_BASE, REP_STOS8_COUNT,         /* stos 8-BIT */
		STOS16, REP_STOS16_BASE, REP_STOS16_COUNT,      /* stos 16-BIT */
		MOVS8, REP_MOVS8_BASE, REP_MOVS8_COUNT,         /* movs 8-BIT */
		MOVS16, REP_MOVS16_BASE, REP_MOVS16_COUNT,      /* movs 16-BIT */

		INS8, REP_INS8_BASE, REP_INS8_COUNT,            /* (80186) ins 8-BIT */
		INS16, REP_INS16_BASE, REP_INS16_COUNT,         /* (80186) ins 16-BIT */
		OUTS8, REP_OUTS8_BASE, REP_OUTS8_COUNT,         /* (80186) outs 8-BIT */
		OUTS16, REP_OUTS16_BASE, REP_OUTS16_COUNT,      /* (80186) outs 16-BIT */
		PUSH_IMM, PUSHA, POPA,                          /* (80186) push IMMEDIATE, pusha/popa */
		IMUL_RRI8, IMUL_RMI8,                           /* (80186) imul IMMEDIATE 8-BIT */
		IMUL_RRI16, IMUL_RMI16,                         /* (80186) imul IMMEDIATE 16-BIT */
		ENTER0, ENTER1, ENTER_BASE, ENTER_COUNT, LEAVE, /* (80186) enter/leave */
		BOUND                                           /* (80186) bound */
	};

	enum SREGS { ES=0, CS, SS, DS };
	enum WREGS { AX=0, CX, DX, BX, SP, BP, SI, DI };

	// construction/destruction
	i8086_common_cpu_device(bus_interface &bus);   // [tek8550] was (mconfig, type, tag, owner, clock)

	// device-level overrides
	virtual void device_start() ATTR_COLD;          // [tek8550] no 'override': no device_t base
	virtual void device_reset() ATTR_COLD;

	// device_execute_interface overrides
	// [tek8550] execute_min/max_cycles and execute_input_edge_triggered removed
	virtual void execute_set_input(int inputnum, int state);

	// device_disasm_interface overrides
	// [tek8550] create_disassembler moved to the public interface
	virtual int get_mode() const override;

	// device_state_interface overrides
	// [tek8550] state_import / state_string_export removed (register accessors above)

	virtual void interrupt(int int_num, int trap = 1);
	virtual bool common_op(uint8_t op);

	// Accessing memory and io
	virtual uint8_t read_byte(uint32_t addr);
	virtual uint16_t read_word(uint32_t addr);
	virtual void write_byte(uint32_t addr, uint8_t data);
	virtual void write_word(uint32_t addr, uint16_t data);
	virtual uint8_t read_port_byte(uint16_t port);
	virtual uint16_t read_port_word(uint16_t port);
	virtual void write_port_byte(uint16_t port, uint8_t data);
	virtual void write_port_byte_al(uint16_t port);
	virtual void write_port_word(uint16_t port, uint16_t data);

	// Executing instructions
	uint8_t fetch_op() { return fetch(); }
	virtual uint8_t fetch() = 0;
	inline uint16_t fetch_word();
	inline uint8_t repx_op();

	// Cycles passed while executing instructions
	inline void CLK(uint8_t op);
	inline void CLKM(uint8_t op_reg, uint8_t op_mem);

	// Memory handling while executing instructions
	virtual uint32_t calc_addr(int seg, uint16_t offset, int size, int op, bool override = true);
	inline uint32_t get_ea(int size, int op);
	inline void PutbackRMByte(uint8_t data);
	inline void PutbackRMWord(uint16_t data);
	inline void RegByte(uint8_t data);
	inline void RegWord(uint16_t data);
	inline uint8_t RegByte();
	inline uint16_t RegWord();
	inline uint16_t GetRMWord();
	inline uint16_t GetnextRMWord();
	inline uint8_t GetRMByte();
	inline void PutMemB(int seg, uint16_t offset, uint8_t data);
	inline void PutMemW(int seg, uint16_t offset, uint16_t data);
	inline uint8_t GetMemB(int seg, uint16_t offset);
	inline uint16_t GetMemW(int seg, uint16_t offset);
	inline void PutImmRMWord();
	inline void PutRMWord(uint16_t val);
	inline void PutRMByte(uint8_t val);
	inline void PutImmRMByte();
	inline void DEF_br8();
	inline void DEF_wr16();
	inline void DEF_r8b();
	inline void DEF_r16w();
	inline void DEF_ald8();
	inline void DEF_axd16();

	// Flags
	inline void set_CFB(uint32_t x);
	inline void set_CFW(uint32_t x);
	inline void set_AF(uint32_t x,uint32_t y,uint32_t z);
	inline void set_SF(uint32_t x);
	inline void set_ZF(uint32_t x);
	inline void set_PF(uint32_t x);
	inline void set_SZPF_Byte(uint32_t x);
	inline void set_SZPF_Word(uint32_t x);
	inline void set_OFW_Add(uint32_t x,uint32_t y,uint32_t z);
	inline void set_OFB_Add(uint32_t x,uint32_t y,uint32_t z);
	inline void set_OFW_Sub(uint32_t x,uint32_t y,uint32_t z);
	inline void set_OFB_Sub(uint32_t x,uint32_t y,uint32_t z);
	inline uint16_t CompressFlags() const;
	inline void ExpandFlags(uint16_t f);

	// rep instructions
	inline void i_insb();
	inline void i_insw();
	inline void i_outsb();
	inline void i_outsw();
	inline void i_movsb();
	inline void i_movsw();
	inline void i_cmpsb();
	inline void i_cmpsw();
	inline void i_stosb();
	inline void i_stosw();
	inline void i_lodsb();
	inline void i_lodsw();
	inline void i_scasb();
	inline void i_scasw();
	inline void i_popf();

	// sub implementations
	inline uint32_t ADDB(uint8_t c = 0);
	inline uint32_t ADDX(uint8_t c = 0);
	inline uint32_t SUBB(uint8_t b = 0);
	inline uint32_t SUBX(uint8_t b = 0);
	inline void ORB();
	inline void ORW();
	inline void ANDB();
	inline void ANDX();
	inline void XORB();
	inline void XORW();
	inline void ROL_BYTE();
	inline void ROL_WORD();
	inline void ROR_BYTE();
	inline void ROR_WORD();
	inline void ROLC_BYTE();
	inline void ROLC_WORD();
	inline void RORC_BYTE();
	inline void RORC_WORD();
	inline void SHL_BYTE(uint8_t c);
	inline void SHL_WORD(uint8_t c);
	inline void SHR_BYTE(uint8_t c);
	inline void SHR_WORD(uint8_t c);
	inline void SHRA_BYTE(uint8_t c);
	inline void SHRA_WORD(uint8_t c);
	inline void XchgAXReg(uint8_t reg);
	inline void IncWordReg(uint8_t reg);
	inline void DecWordReg(uint8_t reg);
	inline void PUSH(uint16_t data);
	inline uint16_t POP();
	inline void JMP(bool cond);
	inline void ADJ4(int8_t param1, int8_t param2);
	inline void ADJB(int8_t param1, int8_t param2);

	union
	{                   /* eight general registers */
		uint16_t w[8];    /* viewed as 16 bits registers */
		uint8_t  b[16];   /* or as 8 bit registers */
	} m_regs;

	enum BREGS {
		AL = NATIVE_ENDIAN_VALUE_LE_BE(0x0, 0x1),
		AH = NATIVE_ENDIAN_VALUE_LE_BE(0x1, 0x0),
		CL = NATIVE_ENDIAN_VALUE_LE_BE(0x2, 0x3),
		CH = NATIVE_ENDIAN_VALUE_LE_BE(0x3, 0x2),
		DL = NATIVE_ENDIAN_VALUE_LE_BE(0x4, 0x5),
		DH = NATIVE_ENDIAN_VALUE_LE_BE(0x5, 0x4),
		BL = NATIVE_ENDIAN_VALUE_LE_BE(0x6, 0x7),
		BH = NATIVE_ENDIAN_VALUE_LE_BE(0x7, 0x6),
		SPL = NATIVE_ENDIAN_VALUE_LE_BE(0x8, 0x9),
		SPH = NATIVE_ENDIAN_VALUE_LE_BE(0x9, 0x8),
		BPL = NATIVE_ENDIAN_VALUE_LE_BE(0xa, 0xb),
		BPH = NATIVE_ENDIAN_VALUE_LE_BE(0xb, 0xa),
		SIL = NATIVE_ENDIAN_VALUE_LE_BE(0xc, 0xd),
		SIH = NATIVE_ENDIAN_VALUE_LE_BE(0xd, 0xc),
		DIL = NATIVE_ENDIAN_VALUE_LE_BE(0xe, 0xf),
		DIH = NATIVE_ENDIAN_VALUE_LE_BE(0xf, 0xe)
	};

	enum {
		I8086_READ,
		I8086_WRITE,
		I8086_FETCH,
		I8086_NONE
	};

	uint16_t  m_sregs[4];

	uint16_t  m_ip;
	uint16_t  m_prev_ip;
	bool      m_io_stall = false;   // I/O wait-state (IOCHRDY) restart pending

	int32_t   m_SignVal;
	uint32_t  m_AuxVal, m_OverVal, m_ZeroVal, m_CarryVal, m_ParityVal; /* 0 or non-0 valued flags */
	uint8_t   m_TF, m_IF, m_DF;     /* 0 or 1 valued flags */
	uint8_t   m_IOPL, m_NT, m_MF;
	uint32_t  m_int_vector;
	uint32_t  m_pending_irq;
	uint32_t  m_nmi_state;
	uint8_t   m_no_interrupt;
	uint8_t   m_fire_trap;
	uint8_t   m_test_state;

	address_space *m_program, *m_opcodes;
	// [tek8550] m_cache8 / m_cache16 removed: fetches go to the bus

	std::function<u8 (offs_t)> m_or8;
	address_space *m_io;
	int m_icount;

	uint32_t m_prefix_seg;   /* the latest prefix segment */
	bool m_seg_prefix;      /* prefix segment indicator */
	bool m_seg_prefix_next; /* prefix segment for next instruction */

	uint32_t m_ea;
	uint16_t m_eo;
	int m_easeg;

	// Used during execution of instructions
	uint8_t   m_modrm;
	uint32_t  m_dst;
	uint32_t  m_src;
	uint32_t  m_pc;

	// Lookup tables
	uint8_t m_parity_table[256];
	struct {
		struct {
			int w[256];
			int b[256];
		} reg;
		struct {
			int w[256];
			int b[256];
		} RM;
	} m_Mod_RM;

	uint8_t m_timing[200];
	uint8_t m_ea_timing[200];
	bool m_halt;

	bool m_lock;
	bool m_lock_pin = false;   // [tek8550] LOCK output asserted
	devcb_write_line m_lock_handler;
};

class i8086_cpu_device : public i8086_common_cpu_device
{
public:
	enum {
		AS_STACK = AS_OPCODES + 1,
		AS_CODE, // data reads from CS are still different from opcode fetches
		AS_EXTRA
	};
	// construction/destruction
	// [tek8550] was (mconfig, tag, owner, clock); i8088 selects the 8-bit data bus
	i8086_cpu_device(bus_interface &bus, bool i8088 = false);
	bool is_8088() const { return m_data_bus_size == 8; }   // [tek8550]

	// [tek8550] memory_space_config and the devcb binders removed; m_out_if_func can be set here
	void set_if_handler(std::function<void(int)> f) { m_out_if_func.f = std::move(f); }

protected:
	int m_data_bus_size;   // [tek8550] was only in the address space configs

	virtual void execute_run() override;
	virtual void device_start() override ATTR_COLD;
	virtual uint8_t fetch() override;
	inline address_space *sreg_to_space(int sreg) const;
	virtual uint8_t read_byte(uint32_t addr) override;
	virtual uint16_t read_word(uint32_t addr) override;
	virtual void write_byte(uint32_t addr, uint8_t data) override;
	virtual void write_word(uint32_t addr, uint16_t data) override;

	// [tek8550] address_space_config members removed
	static const uint8_t m_i8086_timing[200];
	static const uint8_t m_i8086_ea_timing[200];
	devcb_write_line m_out_if_func;
	devcb_write32 m_esc_opcode_handler;
	devcb_write32 m_esc_data_handler;

	address_space *m_stack, *m_code, *m_extra;

	// [tek8550] update_pc() moved to i8086_common_cpu_device (the register setters use it)
};

class i8088_cpu_device : public i8086_cpu_device
{
public:
	// construction/destruction
	i8088_cpu_device(bus_interface &bus);   // [tek8550] was (mconfig, tag, owner, clock)
};


#endif // MAME_CPU_I86_I86_H
