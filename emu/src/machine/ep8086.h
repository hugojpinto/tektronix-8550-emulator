// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8086/8088 emulator processor (three boards, I-III, and the prototype control probe that
// carries the processor). Unlike the Z80 and 8085 boards it is not driven through the
// Emulator Controller's F-ports: the 2650 talks to a kernel the 8086 runs from the board's
// own PROM (160-1312-00 even bytes, 160-1311-00 odd bytes) and function RAM, through a
// command port, a 32-byte common RAM and a master control register. The user's program
// runs on the same processor; a break throws it back into the kernel through NMI.
// Reverse-engineered from the kernel and the 1983 diagnostic's 8086/8088/8087 test
// (Select mode 17, V1.3); see notes/8086.md.
// The 80186/80188 emulator (8300E38) is the same board set with an 80186 and its own kernel
// (160-2525-01 even, 160-2526-01 odd): master control at port 27, and a kernel that decodes
// by address and I/O strobes, the 80186 having no segment status (notes/8086.md, "80186").
#ifndef TEK_MACHINE_EP8086_H
#define TEK_MACHINE_EP8086_H
#include "emu.h"
#include "cpu/i86/i86.h"
#include "cpu/i86/i186.h"
#include "machine/tta.h"
#include <memory>
#include <vector>

class tek8301;

class ep8086_board
{
public:
	explicit ep8086_board(tek8301 &m);
	bool present = false;
	bool is8088 = false;                       // MISC STATUS bit 0 (8088 / 80188)
	bool is186 = false;                        // the 80186/80188 emulator
	bool mhz5 = true;                          // MISC STATUS bit 5: 5 MHz (else 8 MHz)
	std::vector<u8> prom;                      // kernel, interleaved: 2 KB (8086), 4 KB (80186)
	size_t prom_dumped = 0;                    // bytes of the kernel that survive (the 80186's upper half does not)
	bool trace = false;

	// ---- the 2650's side
	u8 master_port() const { return is186 ? 0x27 : 0xa7; }
	bool decodes(u8 port) const { return present && (port == master_port() || (port >= 0x80 && port <= 0x8f)); }
	u8 io_read(u8 port);
	void io_write(u8 port, u8 d);
	// RAMINH: with MASTER<1> set the board's RAMs answer at program memory C000-FFFF in
	// place of program memory
	bool overlay_read(u16 a, u8 &v) const;
	bool overlay_write(u16 a, u8 d);
	// time: the 8086 runs alongside the 2650 (the kernel needs no system bus); in user mode
	// only while the 2650 is halted
	void run(int cycles2650);
	void restart();
	// the run-enable condition the Emulator Controller sees (MASTER<3>)
	bool run_enabled() const { return present && (m_master & 0x08); }
	bool user_mode() const { return m_user; }

	i8086_common_cpu_device &processor() { return *m_cpu; }
	double clock_hz() const { return mhz5 ? 5e6 : 8e6; }

private:
	struct bus_t : i8086_cpu_device::bus_interface {
		ep8086_board *b = nullptr;
		u8   read(int space, u32 a) override;
		void write(int space, u32 a, u8 d) override;
		u8   io_read(u16 port) override;
		void io_write(u16 port, u8 d) override;
		void lock(int state) override;
	} m_bus;
	tek8301 &mdu;
	std::unique_ptr<i8086_common_cpu_device> m_cpu;
	double m_frac = 0;
	u64 m_t = 0;

	// board III: master register, command port, function RAM, common RAM
	u8 m_master = 0;
	u8 m_cmd = 0;
	u8 m_81 = 0;
	u8 m_func[0x1000] = {};
	u8 m_common[0x20] = {};
	// board II: mapping RAM (4 bits) and relocation RAM, one entry per 4 KB block
	u8 m_map[0x100] = {};
	u8 m_reloc[0x100] = {};
	// board I: breakpoint RAMs (4 bits)
	u8 m_bkpt[3][0x100] = {};
	u8 m_reason1 = 0, m_reason2 = 0;

	// kernel / user switching
	bool m_user = false;                       // executing the user's program
	bool m_entering = false;                   // NMI taken: stack writes go to the common RAM
	bool m_stack_latched = false;
	u8 m_stack_latch = 0;                      // port 0: low address bits of the first stack write
	bool m_lock = false;
	int m_locked_pops = 0;
	bool m_kstack = false;
	bool m_capture = false;                    // 80186: writes go to the common RAM from a break to OUT 0E
	u8 kernel_read(int space, u32 a);
	void kernel_write(int space, u32 a, u8 d);
	u8 kernel186_read(u32 a);
	void kernel186_write(u32 a, u8 d);
	void hle186(u32 pc);
	u64 m_a7_shot = 0;                         // MASTER<7> one-shot, until (2650 cycles)
	u32 m_pc_last = 0;
	u8 m_armed = 0, m_svc = 0;
	int m_brk_count = 0;                       // instructions until a scheduled break
	enum { FETCH, READ, WRITE, IOR, IOW };
	bool user_cycle(u32 a, int kind);
	void schedule_break(int after, u8 reason1, u8 reason2);
	void tta_cycle(u32 a, int kind, u8 d);
	bool m_first_fetch = false;
	u32 user_addr(u32 a) const;
	void kernel_reset();
	void request_break(u8 reason1, u8 reason2);
	void enter_kernel();
	u8 user_read(u32 a);
	void user_write(u32 a, u8 d);
};
#endif
