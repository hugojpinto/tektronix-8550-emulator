// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/*** t11: Portable DEC T-11 emulator ******************************************

    Standalone LSI-11 port for the Tektronix 8550 emulator.
    Derived from MAME src/devices/cpu/t11/t11.cpp (see reference/mame/SOURCE.txt).
    Changes: MAME device glue replaced by t11_device::bus_interface;
    only the LSI-11 interrupt priority logic is kept; bus timeouts (NXM)
    are raised by the bus and trap through vector 4; HALT stops the CPU
    (m_halted) so the machine can run ODT, as the KD11 microcode does.

*****************************************************************************/

#include "t11.h"

/* registers of various sizes */
#define REGD(x) m_reg[x].d
#define REGW(x) m_reg[x].w.l
#define REGB(x) m_reg[x].b.l

/* PC, SP, and PSW definitions */
#define SP      REGW(6)
#define PC      REGW(7)
#define SPD     REGD(6)
#define PCD     REGD(7)
#define PSW     m_psw.b.l

t11_device::t11_device(bus_interface &bus)
	: m_bus(bus)
	, c_insn_set(IS_LEIS | IS_EIS | IS_MXPS)   // LSI-11/2 with KEV11 EIS (EIS/FIS is an option)
	, m_initial_pc(0)
	, m_wait_state(0), m_mcir(MCIR_NONE), m_vsel(0), m_cp_state(0)
	, m_vec_active(false), m_pf_active(false), m_berr_active(false), m_hlt_active(false)
	, m_power_fail(false), m_bus_error(false), m_ext_halt(false), m_check_irqs(false), m_trace_trap(false)
	, m_halted(false), m_icount(0), m_insns(0)
{
	for (auto &reg : m_reg) reg.d = 0;
	m_psw.d = 0; m_ppc.d = 0;
	m_cp[0] = m_cp[1] = m_cp[2] = m_cp[3] = false;
}

/*************************************
 *  Low-level memory operations
 *************************************/

int t11_device::ROPCODE()
{
	bool nxm = false;
	int val = m_bus.read16(PC & 0xfffe, nxm);
	if (nxm) { m_bus_error = true; m_check_irqs = true; }
	PC += 2;
	return val;
}

int t11_device::RBYTE(int addr)
{
	bool nxm = false;
	u16 w = m_bus.read16(addr & 0xfffe, nxm);
	if (nxm) { m_bus_error = true; m_check_irqs = true; }
	return (addr & 1) ? (w >> 8) : (w & 0xff);
}

void t11_device::WBYTE(int addr, int data)
{
	bool nxm = false;
	m_bus.write8(addr & 0xffff, u8(data), nxm);
	if (nxm) { m_bus_error = true; m_check_irqs = true; }
}

int t11_device::RWORD(int addr)
{
	bool nxm = false;
	int val = m_bus.read16(addr & 0xfffe, nxm);
	if (nxm) { m_bus_error = true; m_check_irqs = true; }
	return val;
}

void t11_device::WWORD(int addr, int data)
{
	bool nxm = false;
	m_bus.write16(addr & 0xfffe, u16(data), nxm);
	if (nxm) { m_bus_error = true; m_check_irqs = true; }
}

/*************************************
 *  Low-level stack operations
 *************************************/

void t11_device::PUSH(int val)
{
	SP -= 2;
	WWORD(SPD, val);
}

int t11_device::POP()
{
	int result = RWORD(SPD);
	SP += 2;
	return result;
}

/*************************************
 *  Flag definitions and operations
 *************************************/

#define CFLAG 1
#define VFLAG 2
#define ZFLAG 4
#define NFLAG 8

#define GET_C (PSW & CFLAG)
#define GET_V (PSW & VFLAG)
#define GET_Z (PSW & ZFLAG)
#define GET_N (PSW & NFLAG)
#define GET_T (PSW & (1 << 4))
#define GET_I (PSW & (1 << 7))

#define CLR_C (PSW &= ~CFLAG)
#define CLR_V (PSW &= ~VFLAG)
#define CLR_Z (PSW &= ~ZFLAG)
#define CLR_N (PSW &= ~NFLAG)

#define SET_C (PSW |= CFLAG)
#define SET_V (PSW |= VFLAG)
#define SET_Z (PSW |= ZFLAG)
#define SET_N (PSW |= NFLAG)

/*************************************
 *  Interrupt handling
 *************************************/

// priorities from LSI-11 Processor Handbook, p. 4-70
void t11_device::t11_check_irqs()
{
	s8  mcir = MCIR_NONE;
	u16 vsel = 0;

	if (m_mcir != MCIR_NONE)
	{
		mcir = m_mcir;
		vsel = m_vsel;
	}
	// 1. bus error
	else if (m_bus_error)
	{
		m_bus_error = false;
		mcir = MCIR_IRQ;
		vsel = T11_TIMEOUT;
	}
	// 4. trace trap
	else if (m_trace_trap)
	{
		if (GET_T)
		{
			mcir = MCIR_IRQ;
			vsel = T11_BPT;
		}
		else
			m_trace_trap = false;
	}
	else if (GET_T)
	{
		m_trace_trap = true;
	}
	// 5. halt line (B HALT L)
	else if (m_ext_halt)
	{
		m_ext_halt = false;
		m_halted = true;
		m_icount = 0;
	}
	// 6. power fail (B POK H)
	else if (m_power_fail)
	{
		m_power_fail = false;
		mcir = MCIR_IRQ;
		vsel = T11_PWRFAIL;
	}
	// 7. event line (B EVNT L)
	else if (m_cp[2] && !GET_I)
	{
		m_cp[2] = false;
		mcir = MCIR_IRQ;
		vsel = VM1_EVNT;
	}
	// 8. device interrupt (B IRQ4 L)
	else if (m_vec_active && !GET_I)
	{
		int vec = m_bus.iack();
		if (vec == -1 || vec == 0)
		{
			m_vec_active = false;
			m_mcir = MCIR_NONE;
			return;
		}
		mcir = MCIR_IRQ;
		vsel = u16(vec);
	}

	if (mcir == MCIR_IRQ)
		take_interrupt(vsel);

	m_mcir = MCIR_NONE;
}

void t11_device::take_interrupt(u16 vector)
{
	// fetch the new PC and PSW from that vector
	u16 new_pc = RWORD(vector);
	u16 new_psw = RWORD(vector + 2);

	// push the old state, set the new one
	PUSH(PSW);
	PUSH(PC);
	PCD = new_pc;
	PSW = new_psw;

	m_icount -= 114;
	m_wait_state = 0;
}

/*************************************
 *  Core opcodes
 *************************************/

#include "t11table.hxx"
#include "t11ops.hxx"

/*************************************
 *  Reset / inputs / execution
 *************************************/

void t11_device::reset(u16 start_pc, u16 start_psw)
{
	m_initial_pc = start_pc;
	PC = start_pc;
	PSW = u8(start_psw);
	m_wait_state = 0;
	m_power_fail = false;
	m_bus_error = false;
	m_ext_halt = false;
	m_trace_trap = false;
	m_check_irqs = false;
	m_halted = false;
	m_mcir = MCIR_NONE;
	m_vsel = 0;
	m_cp[0] = m_cp[1] = m_cp[2] = m_cp[3] = false;
	m_vec_active = false;
}

void t11_device::set_input(int irqline, int state)
{
	switch (irqline)
	{
	case CP0_LINE: case CP1_LINE: case CP2_LINE: case CP3_LINE:
		if (state == CLEAR_LINE)
			m_cp_state &= ~(1 << irqline);
		else
		{
			m_cp_state |= 1 << irqline;
			m_cp[irqline] = true;
		}
		break;
	case VEC_LINE:
		m_vec_active = (state != CLEAR_LINE);
		break;
	case PF_LINE:
		if (state != CLEAR_LINE && !m_pf_active) m_power_fail = true;
		m_pf_active = (state != CLEAR_LINE);
		break;
	case BUS_ERROR:
		if (state != CLEAR_LINE && !m_berr_active) m_bus_error = true;
		m_berr_active = (state != CLEAR_LINE);
		break;
	case HLT_LINE:
		if (state != CLEAR_LINE && !m_hlt_active) m_ext_halt = true;
		m_hlt_active = (state != CLEAR_LINE);
		break;
	}
}

int t11_device::run(int cycles)
{
	if (m_halted) return cycles;
	m_icount = cycles;

	t11_check_irqs();

	if (m_wait_state)
	{
		m_icount = 0;
		return cycles;
	}

	while (m_icount > 0 && !m_halted)
	{
		m_ppc = m_reg[7];
		if (trace_hook) trace_hook(PC);
		u16 op = ROPCODE();
		(this->*s_opcode_table[op >> 3])(op);
		m_insns++;

		// external lines (EVNT / IRQ) are sampled between instructions
		if (m_check_irqs || m_trace_trap || GET_T || m_bus_error || m_ext_halt ||
			(!GET_I && (m_vec_active || m_cp[2])))
		{
			m_check_irqs = false;
			t11_check_irqs();
		}
		if (m_wait_state) { m_icount = 0; break; }
	}
	return cycles - m_icount;
}
