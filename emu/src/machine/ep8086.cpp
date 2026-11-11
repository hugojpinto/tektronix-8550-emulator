// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8086/8088 emulator processor -- see ep8086.h and notes/8086.md.
#include "machine/ep8086.h"
#include "machine/tek8301.h"
#include <cstdio>
#include <cstring>

ep8086_board::ep8086_board(tek8301 &m) : mdu(m)
{
	m_bus.b = this;
	m_cpu = std::make_unique<i8086_cpu_device>(m_bus, false);
}

void ep8086_board::restart()
{
	m_master = 0; m_cmd = 0; m_81 = 0;
	m_reason1 = m_reason2 = 0;
	kernel_reset();
}

void ep8086_board::kernel_reset()
{
	m_cpu = std::make_unique<i8086_cpu_device>(m_bus, is8088);
	m_cpu->reset();
	m_user = false; m_entering = false; m_stack_latched = false; m_lock = false;
}

// ---------------------------------------------------------------------------
// 2650 side

u8 ep8086_board::io_read(u8 port)
{
	u8 v = 0x00;
	switch (port) {
	// MASTER CONTROL reads back bits 6-0; bit 7 reads 1 for a while after each write (a one-
	// shot: the diagnostic reads it set right after writing, clear after a ~0.1 s delay, module 10)
	case 0xa7: v = u8((m_master & 0x7f) | (mdu.now_cycles() < m_a7_shot ? 0x80 : 0x00)); break;
	case 0x80: v = m_cmd; break;                     // COMMAND PORT
	case 0x87:                                       // MISC STATUS (diag module 12): 1,2,3,4,6 = 1,
		v = u8(0x5e | (is8088 ? 0x01 : 0x00) | (mhz5 ? 0x00 : 0x20));   // 0 = 8088, 5 = 8 MHz
		break;
	// BREAK REASON1: bit 0 processor reset (MASTER<0>), bit 7 with MASTER<6:5> != 0 (a prototype
	// mode with no prototype attached: inferred, module 13)
	case 0x88: v = u8(m_reason1 | (m_master & 0x01) | ((m_master & 0x60) ? 0x80 : 0x00)); break;
	// PC LAST: the address of the last instruction the user's program started, active low
	case 0x8d: v = u8(~m_pc_last); break;
	case 0x8e: v = u8(~(m_pc_last >> 8)); break;
	case 0x8f: v = u8(~(m_pc_last >> 16)); break;
	case 0x89: v = m_reason2; break;
	case 0x8c: v = m_svc; break;                     // SVC register (inferred: the matched cycle's A7-0)
	default: v = 0x00; break;
	}
	if (trace) fprintf(stderr, "[86 2650 rd %02x = %02x]\n", port, v);
	return v;
}

void ep8086_board::io_write(u8 port, u8 d)
{
	if (trace) fprintf(stderr, "[86 2650 wr %02x <- %02x]\n", port, d);
	switch (port) {
	case 0xa7: {
		const u8 old = m_master;
		m_master = u8(d & 0x7f);
		m_a7_shot = mdu.now_cycles() + u64(tek8301::CPU_HZ / 1000);   // 1 ms (duration not known)
		if (m_master & 0x01) { kernel_reset(); m_cmd = 0; m_reason1 = m_reason2 = 0; }   // RESET held; clears the command port and the break reasons (modules 11, 14)
		else if (old & 0x01) kernel_reset();                    // released: the kernel starts at FFFF0
		break;
	}
	case 0x80: m_cmd = d; break;
	case 0x81: m_81 = d; break;
	default: break;
	}
}

// RAMINH: C000-C0FF / C800-C8FF / D000-D0FF breakpoint RAMs, D800-D8FF mapping RAM,
// E000-E0FF relocation RAM, F800-F81F common RAM (diag modules 20-24)
bool ep8086_board::overlay_read(u16 a, u8 &v) const
{
	if (!present || !(m_master & 0x02) || a < 0xc000) return false;
	switch (a & 0xf800) {
	case 0xc000: v = u8(m_bkpt[0][a & 0xff] | 0xf0); break;
	case 0xc800: v = u8(m_bkpt[1][a & 0xff] | 0xf0); break;
	case 0xd000: v = u8(m_bkpt[2][a & 0xff] | 0xf0); break;
	case 0xd800: v = u8(m_map[a & 0xff] | 0xf0); break;
	case 0xe000: v = m_reloc[a & 0xff]; break;
	case 0xf800: v = m_common[a & 0x1f]; break;
	default: v = 0xff; break;
	}
	return true;
}

bool ep8086_board::overlay_write(u16 a, u8 d)
{
	if (!present || !(m_master & 0x02) || a < 0xc000) return false;
	switch (a & 0xf800) {
	case 0xc000: m_bkpt[0][a & 0xff] = d & 0x0f; break;
	case 0xc800: m_bkpt[1][a & 0xff] = d & 0x0f; break;
	case 0xd000: m_bkpt[2][a & 0xff] = d & 0x0f; break;
	case 0xd800: m_map[a & 0xff] = d & 0x0f; break;
	case 0xe000: m_reloc[a & 0xff] = d; break;
	case 0xf800: m_common[a & 0x1f] = d; break;
	default: break;
	}
	return true;
}

// ---------------------------------------------------------------------------
// the 8086's side

// The user's memory: 4 KB blocks relocated into program memory (relocation RAM, indexed by
// A19-12). Mapping RAM bits (diag modules 56, 57): 0 non-existent memory, 2 write protect.
u32 ep8086_board::user_addr(u32 a) const
{
	return u32((m_reloc[(a >> 12) & 0xff] << 12) | (a & 0xfff)) & 0xffff;
}

u8 ep8086_board::user_read(u32 a)
{
	return mdu.progmem(u16(user_addr(a)));
}

void ep8086_board::user_write(u32 a, u8 d)
{
	mdu.progmem_w(u16(user_addr(a)), d);
}

// Board I watches each bus cycle of the user's program. Breakpoints: three RAMs, one bit per
// breakpoint (01 BK1, 02 BK2, 04 BK3, 08 SVC), indexed by A19-12 (C000), A11-4 (C800) and
// by the cycle type with A3-0 (D000: high nibble bit 4 = memory, bit 5 = write); a
// breakpoint matches when all three have its bit (diag modules 51-55, routine 3BF1).
// A matched cycle breaks the program one instruction later; an opcode fetch counts as part
// of the instruction before (the 8086 prefetches), so it breaks after the fetched one.
// A non-existent-memory or write-protect cycle breaks after the instruction that made it.
bool ep8086_board::user_cycle(u32 a, int kind)
{
	if (!m_user || m_entering) return true;
	bool allow = true;
	const bool mem = kind != IOR && kind != IOW, wr = kind == WRITE || kind == IOW;
	if (mem) {
		const u8 map = m_map[(a >> 12) & 0xff];
		if ((map & 0x01) && kind != FETCH) schedule_break(1, 0x00, 0x02);
		if ((map & 0x04) && wr) { schedule_break(2, 0x00, 0x04); allow = false; }
	}
	// D000's index: A3-0, bit 4 memory, bit 5 write, and the arming flip-flops fed back:
	// bit 7 = BK1 has occurred, bit 6 = BK2 has occurred. An armed breakpoint is one whose
	// D000 entries require them (BK 2 ... -A); port 81 bits 3/4 (clear) make BK1/BK2 arm only,
	// without breaking (diag module 54: sequence 33E0 breaks, the out-of-order 33E8 must not)
	const u8 type = u8((mem ? 0x10 : 0) | (wr ? 0x20 : 0) | m_armed);
	const u8 hit = u8(m_bkpt[0][(a >> 12) & 0xff] & m_bkpt[1][(a >> 4) & 0xff] & m_bkpt[2][type | (a & 0x0f)]);
	if (hit && trace) fprintf(stderr, "[86 bkpt hit %x at %05x kind %d, 81=%02x armed %02x]\n", hit, unsigned(a), kind, m_81, m_armed);
	if (hit) {
		u8 r1 = 0;
		if (hit & 0x01) { m_armed |= 0x80; if (m_81 & 0x08) r1 |= 0x10; }
		if (hit & 0x02) { m_armed |= 0x40; if (m_81 & 0x10) r1 |= 0x08; }
		if (hit & 0x04) r1 |= 0x04;
		if (hit & 0x08) { r1 |= 0x20; m_svc = u8(a); }
		if (r1) schedule_break(kind == FETCH ? 1 : 2, r1, 0x00);
	}
	return allow;
}

// Board I's TTA connectors: each bus cycle of the program as the TTA acquires it. The bus
// status byte, decoded by the 8540's 8088 support (160-1461, table after "CS]ES]DS]SS]"):
// bit 0 first byte of an instruction, bits 3-1 S2-S0 (000 IA, 001 IR, 010 IW, 011 HT,
// 100 fetch, 101 RD, 110 WT), bits 5-4 segment (11 ES, 10 SS, 01 CS, 00 DS), bit 6 BHE
// and bit 7 TEST, both active low. Activity byte: bit 7 INT, bit 6 LOCK (active low),
// bit 0 set (diag module 70 expects 01 under mask 83). Fetches are the instruction bytes
// as the queue hands them out (module 70: one per byte, 19 then 18).
void ep8086_board::tta_cycle(u32 a, int kind, u8 d)
{
	tta &t = mdu.tta_board;
	if (!m_user || m_entering || !t.target_registers()) return;
	static const u8 s20[] = { 0x08, 0x0a, 0x0c, 0x02, 0x04 };   // FETCH READ WRITE IOR IOW -> S2-S0 << 1
	const int seg = kind == FETCH ? 1 : kind == IOR || kind == IOW ? 1 : m_cpu->bus_segment();   // ES 0 CS 1 SS 2 DS 3
	static const u8 segbits[] = { 0x30, 0x10, 0x20, 0x00 };
	tta::sample s;
	s.addr = a & (kind == IOR || kind == IOW ? 0xffff : 0xfffff);
	s.data = d;
	s.bus = u8(s20[kind] | segbits[seg & 3] | (kind == FETCH && m_first_fetch ? 0x01 : 0x00) | ((a & 1) ? 0x00 : 0x40));
	s.activity = u8(0x01 | (m_lock ? 0x00 : 0x40));
	if (kind == FETCH) m_first_fetch = false;
	t.target_cycle(mdu.now_cycles(), s);
	if (t.take_target_break()) schedule_break(kind == FETCH ? 1 : 2, 0x00, 0x08);
}

// the reason latches are set when the condition occurs; the break itself follows
void ep8086_board::schedule_break(int after, u8 reason1, u8 reason2)
{
	m_reason1 |= reason1; m_reason2 |= reason2;
	if (!m_brk_count || after < m_brk_count) m_brk_count = after;
}

u8 ep8086_board::bus_t::read(int space, u32 a)
{
	ep8086_board &k = *b;
	// a break: the NMI vector fetch (00008-0000B) is where the board takes the processor back
	// into the kernel (the Emulator Specifics: "addresses 8, 9, A and B are the vector that
	// points to an NMI ... used by the operating system whenever a break is generated")
	if (k.m_entering && space == i8086_cpu_device::SP_CODE && (a & 0xffffc) == 0x00008) {
		if (k.m_user) k.enter_kernel();
		if (a == 0x0000b) k.m_entering = false;
	}
	if (k.m_user) {
		const int kind = space == i8086_cpu_device::SP_OPCODES ? FETCH : READ;
		k.user_cycle(a, kind);
		const u8 v = k.user_read(a);
		if (!k.m_entering) k.tta_cycle(a, kind, v);
		return v;
	}
	switch (space) {
	case i8086_cpu_device::SP_OPCODES: return k.prom.empty() ? 0xff : k.prom[a & 0x7ff];
	case i8086_cpu_device::SP_STACK: if (k.m_lock) k.m_locked_pops++; return k.m_common[a & 0x1f];
	case i8086_cpu_device::SP_EXTRA: if (k.m_lock) return k.user_read(a); break;
	default: break;
	}
	if (a & 0xf000) return k.m_func[a & 0xfff];          // function RAM: 4 KB, above 0FFF (module 33 reaches it at 2000)
	if (a & 0x0800) {
		switch (a & 0x7ff) {
		case 0: return k.m_cmd;
		case 2: return u8(k.mdu.cpu.halted() && (k.m_master & 0x08) ? 0x01 : 0x00);   // run granted (inferred)
		default: return 0xff;
		}
	}
	return k.prom.empty() ? 0xff : k.prom[a & 0x7ff];
}

void ep8086_board::bus_t::write(int space, u32 a, u8 d)
{
	ep8086_board &k = *b;
	if (k.m_entering && space == i8086_cpu_device::SP_STACK) {
		if (!k.m_stack_latched) { k.m_stack_latched = true; k.m_stack_latch = u8(a); }
		k.m_common[a & 0x1f] = d; return;
	}
	if (k.m_user) { if (k.user_cycle(a, WRITE)) k.user_write(a, d); k.tta_cycle(a, WRITE, d); return; }
	switch (space) {
	case i8086_cpu_device::SP_STACK: k.m_common[a & 0x1f] = d; return;
	case i8086_cpu_device::SP_EXTRA: if (k.m_lock) { k.user_write(a, d); return; } break;
	default: break;
	}
	if (a & 0xf000) { k.m_func[a & 0xfff] = d; return; }
	if (a & 0x0800) {
		if ((a & 0x7ff) == 0) k.m_cmd = 0;               // the kernel acknowledges a command
		return;
	}
}

u8 ep8086_board::bus_t::io_read(u16 port)
{
	ep8086_board &k = *b;
	if (k.m_user) { k.user_cycle(port, IOR); k.tta_cycle(port, IOR, 0xff); }
	if (k.m_user || k.m_lock) return 0xff;              // the prototype's I/O (none in mode 0)
	if (port == 0) return k.m_stack_latch;
	return 0xff;
}

void ep8086_board::bus_t::io_write(u16 port, u8 d)
{
	ep8086_board &k = *b;
	if (k.m_user) { k.user_cycle(port, IOW); k.tta_cycle(port, IOW, d); }
}

// LOCK marks the kernel's accesses to the user's side (ES memory, I/O). A LOCKed IRET (its
// three pops come from the common RAM) returns to the user's program: the board switches to
// user space for the fetch that follows (inferred from the kernel's GO path, 036B-0386)
void ep8086_board::bus_t::lock(int state)
{
	ep8086_board &k = *b;
	k.m_lock = state != 0;
	if (state) { k.m_locked_pops = 0; return; }
	if (!k.m_user && k.m_locked_pops >= 6) {
		k.m_user = true;
		k.m_armed = 0; k.m_brk_count = 0; k.m_reason1 = k.m_reason2 = 0;
		if (k.trace) fprintf(stderr, "[86 user mode at %05x]\n", unsigned(k.m_cpu->pc()));
	}
}

// a break: NMI, with the stack writes of the interrupt diverted to the common RAM
void ep8086_board::request_break(u8 reason1, u8 reason2)
{
	if (!m_user || m_entering) return;
	m_reason1 |= reason1; m_reason2 |= reason2;
	m_entering = true; m_stack_latched = false;
	m_cpu->set_input_line(INPUT_LINE_NMI, ASSERT_LINE);
	m_cpu->set_input_line(INPUT_LINE_NMI, CLEAR_LINE);
}

void ep8086_board::enter_kernel()
{
	m_user = false;
	if (trace) fprintf(stderr, "[86 break: kernel, pc last %05x]\n", unsigned(m_pc_last));
	// the 2650 waiting in HALT is woken: the emulator has stopped
	if (mdu.cpu.halted()) mdu.ec_interrupt(tek8301::INT_SLV_HALT, false);
}

void ep8086_board::run(int cycles2650)
{
	if (!present || (m_master & 0x01) || prom.empty()) return;
	m_frac += cycles2650 * (clock_hz() / tek8301::CPU_HZ);
	while (m_frac > 0) {
		// the program runs while the 2650 waits in HALT; when the 2650 resumes (an interrupt)
		// the board breaks the program (REASON1 = 00: diag module 40)
		if (m_user && !m_entering && !mdu.cpu.halted()) request_break(0, 0);
		const bool user = m_user && !m_entering;
		if (user) {
			// an instruction in non-existent memory is not executed: its fetch breaks first
			if (m_map[(m_cpu->pc() >> 12) & 0xff] & 0x01) { request_break(0, 0x02); continue; }
			m_pc_last = m_cpu->pc();
			m_first_fetch = true;
		}
		int c = 0;
		do { const int n = m_cpu->run(1); c += n > 0 ? n : 1; } while (m_cpu->mid_instruction());
		m_t += u64(c);
		m_frac -= c;
		if (user && m_user && !m_entering) {
			if (m_brk_count && --m_brk_count == 0) request_break(0, 0);
			else if (m_master & 0x04) request_break(0x40, 0x00);   // trace (MASTER<2>): after each instruction
		}
	}
}
