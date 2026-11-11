// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix 8301 Microprocessor Development Unit (2650 system processor),
// part of the 8550 MDL. Boards modelled:
//   System Controller : 2650, boot/diag ROM overlay (EA), bank switch (EE),
//                       3x MC6850, IM6402 HSI UART + 8257 DMA, interval timer,
//                       LEDs/mode switch, interrupt encoder
//   System RAM        : 64 KB, paging of 2000-3FFF (D3), refresh/parity diag (D2)
//   Program Memory    : 64 KB (two 32 KB boards, relocation regs B8/B9 stored)
//   Language Processor: Z80A on program memory (the mode-0 "emulator")
//   Emulator Controller (subset): master/slave switching, SVC detection,
//                       slave-halted interrupt, F2 pending, PC next/last,
//                       forced reset/jump. Not yet: breakpoints, single cycle,
//                       memory map/write protect, external emulator boards.
#ifndef TEK_TEK8301_H
#define TEK_TEK8301_H
#include "emu.h"
#include "cpu/s2650/s2650.h"
#include "machine/mc6850.h"
#include "machine/i8257.h"
#include "machine/prototype.h"
extern "C" {
#include "cpu/z80/superzazu/z80.h"
}
#include <vector>
#include "machine/prom_programmer.h"
#include "machine/rtpa.h"
#include "machine/tta.h"
#include "machine/emulator_specifics.h"
#include "machine/ep8085.h"
#include "cpu/i8085/i8085.h"
#include <deque>
#include <string>
#include <functional>

class z80_slave;

class tek8301 : public s2650_device::bus_interface
{
public:
	static constexpr double CPU_HZ = 2'000'000.0;   // FAST clock (J5704)
	// Emulator Controller interrupts (8301 Table 7-x)
	static constexpr int INT_SVC1 = 16, INT_BP1 = 24, INT_BP2 = 25, INT_SCY = 26, INT_SLV_HALT = 27, INT_DIAG = 28, INT_RTPA = 29;

	tek8301();
	~tek8301();
	bool load_roms(const std::string &dir, std::string &err);
	void power_on();
	void restart();
	void run_slice(int cycles);

	s2650_device cpu;
	mc6850 remote, aux, term;     // CA/CB, CC/CD, CE/CF
	bool pm_ext_comparator = false;   // see Program Memory notes below (-pm ext)
	u8 mode_switch = 0x00;        // ED read D7-D2 (1 = open); all closed = normal boot
	bool trace_io = false;

	// HSI link (to the 8501 I/O board). Bytes we send go to hsi_tx; the peer
	// delivers bytes with hsi_rx(). Pacing: one byte per hsi_char_cycles.
	std::function<void(u8)> hsi_tx;
	void hsi_rx(u8 b) { m_hsi_rxq.push_back(b); }
	bool hsi_connected = false;
	std::function<bool()> hsi_cts;           // peer's receiver ready (8501 RCSR NCTS = 0)
	bool hsi_rx_busy() const { return m_hsi_da || !m_hsi_rxq.empty(); }
	int  hsi_char_cycles = 150;   // 153.6 kbaud, ~11 bits/char at 2 MHz

	u8 leds() const { return m_led; }
	u64 dma_transfers = 0;        // bytes moved by the 8257 (front panel DMA lamp)
	u8 ram_leds() const { return m_d2; }
	u64 cycles() const { return m_cycles; }

	// s2650 bus
	u8 read(offs_t a) override;
	void write(offs_t a, u8 d) override;
	u8 io_read(offs_t port) override;
	void io_write(offs_t port, u8 d) override;
	int intack() override;
	// SENSE input: a ~25 Hz square wave (20 ms half period). Inferred from the disk
	// diagnostic's processor-speed test, which counts 27-clock loops per half period
	// and accepts a high byte of 5 (2 MHz) or 3 (1.25 MHz); 1200 Hz baud clock / 48.
	int sense() override { return int((m_cycles / u64(CPU_HZ * 0.020)) & 1); }

	// Language Processor / Emulator Controller
	z80 lp;   // Language Processor (E7)
	z80 ep;   // Z80 emulator processor board (E3) -- firmware high-level emulated
	// the Emulator Controller's slaves: emulator processor boards and the Language Processor
	// (machine/emulator_board.h); the Emulator Controller's behaviour around them is slave_run
	friend class emulator_board; friend class ep8085_board; friend class z80_slave;
	ep8085_board m_ep85{*this};               // 8085 board (8300E06, control port E5)
	std::unique_ptr<z80_slave> m_slave_ep, m_slave_lp;   // Z80 board (E3), Language Processor (E7)
	ep8085_board &board8085() { return m_ep85; }
	emulator_board *active_slave() const;      // the one the Emulator Controller drives, if any
	void slave_run(emulator_board &b, int cycles2650);
	std::unique_ptr<emulator_specifics> m_spec_z80 = make_z80_specifics(ep);
	prototype_bus *target = &m_no_target;   // probe side (modes 1/2)
	// E3 (emulator board control), as DOS/50 V1.0 drives it: 80 firmware call / 92 run in mode 0,
	// C0 firmware call / B2 run in modes 1-2 -> bit 6 or bit 5 selects the prototype
	bool proto_mode() const { return ep_enabled() && (m_e3 & 0x60); }
	bool proto_all() const { return ep_enabled() && (m_e3 & 0x60) == 0x60; }   // mode 2 run (F2): all memory is the prototype's
	bool ep_present = true;
	prom_programmer prom;      // 8550F30 PROM programmer controller (ports D0/D1, ints 13/14)
	rtpa rtpa_board;           // Real-Time Prototype Analyzer option (ports C0-C7)
	tta tta_board;             // Trigger Trace Analyzer option (ports 70-77, RAM at E800-F9FF)
	double m_seconds = 0;      // for traces
	int m_chunk = 0; bool m_in_cpu = false;
	bool m_master_was_halted = false;
	// 2650 clocks so far, exact to the instruction while the CPU runs (the current
	// instruction's cycles are already counted: MAME charges them before executing)
	u8 bank_lines() const { return u8(~m_f4); }   // extended address A23-A16 as driven (F4 is written active low)
	u64 now_cycles() const { return m_in_cpu ? m_cycles + u64(m_chunk - cpu.icount()) : m_cycles; }
	bool prom_dos50_fix = false;   // -prom fix: adapt PROM software V2.1 to DOS/50 V1.0 (see notes)
	std::vector<u8> ep_prom;   // optional dump of the Z80 emulator board's firmware PROM (512 bytes)
	bool ep_enabled() const { return ep_present && (m_e3 & 0x80); }
	// the active emulator processor's specifics (disassembler, DOS/50 trace line, clock); none without a board
	const emulator_specifics *emulator() const;
	bool slave_enabled() const { return active_slave() != nullptr; }
	bool slave_halted() const;
	// slave runs while the master is halted and no debug interrupt is outstanding
	bool lp_running() const { return slave_enabled() && cpu.halted() && !slave_halted() && m_lp_live && !m_ec_pend && !m_svc_stop && !(m_pending & 0x00ff0000u) && !rtpa_board.stopped() && !tta_board.stopped(); }
	bool ep_trace = false;
	u16 lp_pc_last() const { return m_lp_pc_last; }
	u8 progmem(u16 a) const { return m_progmem[a]; }
	u8 sysmem(u16 a) const { return m_sysram[a]; }
	void progmem_w(u16 a, u8 d) { m_progmem[a] = d; }
	std::function<void(u16)> lp_trace_hook;
	std::string lp_state() const {
		return util::string_format("LP: e7=%02x live=%d z80halt=%d pc=%04x ec_pend=%02x f8=%02x f9=%02x pend=%08x 2650halt=%d",
			m_e7, m_lp_live, int(lp.halted), lp.pc, m_ec_pend, m_f8, m_f9, m_pending, int(cpu.halted()));
	}

	u8 peek(offs_t a) { return mem_rd(cpu_to_bus(a & 0x7fff, false), false); }
	void poke(offs_t a, u8 d) { mem_wr(cpu_to_bus(a & 0x7fff, true), d); }   // debugging (-poke)

private:
	std::vector<u8> m_boot, m_diag, m_sysram, m_progmem;
	std::vector<u8> m_par;        // per-byte: parity-complement state when written
	u16 m_par_addr = 0;
	u8 m_e3 = 0;
	bool m_svc_stop = false;   // an SVC stops the slave until the next F9 write (Z80 diag 07/0070)
	bool m_fr_pending = false, m_fj_pending = false, m_fi_pending = false;   // forced reset / jump / interrupt
	u8 m_f5 = 0xff;
	void ep_firmware(u16 entry);
	u8 m_ea = 0, m_ee = 0, m_d2 = 0, m_d3 = 0, m_e9 = 0, m_ec = 0, m_led = 0, m_e7 = 0;
	u8 m_b8 = 0, m_b9 = 0, m_eb_out = 0;
	u8 m_f4 = 0xff;               // Emulator Controller extended bank (A16-A23) for system-side accesses
	// Program Memory: two 32 KB boards (low/B8, high/B9) strapped at extended bank 00.
	// pm_ext_comparator = true models the 1981+ boards with an extended-bank comparator:
	// with B8/B9 D6 = 0 a board answers only when A16-A23 = 00. The system processor's
	// A16-A23 come from F4, which is active low (the memory diagnostic sweeps F4 from FF
	// downwards; F4 = FF is bank 00). Bus masters without extended addressing (8257 DMA,
	// the Language Processor) always reach the boards.
	// false (default) models earlier boards without the comparator: DOS/50 V1.0 (1980)
	// writes F4 = 00 and B8 = 10 and only works that way. The 1983 diagnostic disk
	// needs true (-pm ext).
	static constexpr u8 PROG_BANK = 0xff;   // F4 value (active low) that selects bank 00
	bool prog_present(u8 ext, u16 a) const {
		if (!pm_ext_comparator) return true;
		u8 c = (a & 0x8000) ? m_b9 : m_b8;
		return (c & 0x40) || ext == PROG_BANK;
	}
	u8 prog_rd(u8 ext, u16 a) const {
		// Z80 emulator board firmware PROM: 512 bytes at F800-F9FF of program memory while
		// the board is in firmware mode (E3<7> = 1, E3<4> = 0). Not dumped: reads FF unless
		// -ep-prom supplies an image (Z80 diag 07/00D0 checksums it, expecting 0852).
		if (ep_present && (m_e3 & 0x90) == 0x80 && (a & 0xfe00) == 0xf800) return ep_prom.empty() ? 0xff : ep_prom[(a - 0xf800) % ep_prom.size()];
		{ u8 v; if (m_ep85.prom_window(a, v)) return v; }   // other boards' firmware PROMs
		return prog_present(ext, a) ? m_progmem[a] : 0xff;
	}
	void prog_wr(u8 ext, u16 a, u8 d) { if (prog_present(ext, a)) m_progmem[a] = d; }
	u64 m_cycles = 0, m_next_timer = 0, m_next_refresh = 0;
	u32 m_pending = 0;            // latched interrupt flip-flops (bit n = int n)

	// Emulator Controller state
	u8  m_ec_pend = 0;            // F2 bits (1 = pending; read inverted): 7 BP1 6 BP2 5 S/CY 4 HALT 3 DIAG 2-0 INT29-31
	u8  m_f3 = 0x0f, m_f8 = 0x1f, m_f9 = 0x1f, m_fa = 0, m_fb = 0;
	u16 m_bp[2] = {0, 0};          // breakpoint addresses (FD:FC, FF:FE)
	u8  m_bp_ext[2] = {0, 0};      // F6/F7 (A16-23; ignored for the 16-bit Z80)
	u8  m_bp_hit = 0;              // bit0 BP1, bit1 BP2 (during the current slave instruction)
	void bp_check(u16 a, bool write);
	u16 m_lp_pc_last = 0, m_lp_pc_next = 0;
	bool m_lp_live = false;       // Z80 out of reset (forced reset / activation done)
	double m_lp_frac = 0;
	void lp_reset();
	void z80_setup(z80 &z);
	void lp_run(int cycles2650);
	void ec_interrupt(int n, bool latch_pc = true);     // n = 16..31
	void tta_interrupt();
	static u8 lp_read(void *u, u16 a) {
		auto *m = static_cast<tek8301 *>(u); m->bp_check(a, false);
		bool m1 = m->lp.m1 || m->ep.m1;
		u8 v = m->slave_rd(a, m1);
		if (m->m_rtpa_watch) m->rtpa_board.bus(a, v, m1, false, false);
		if (m->m_tta_watch) m->tta_board.target_bus(m->ep.cyc, a, v, m1 ? tta::FETCH : tta::MREAD);
		return v;
	}
	static void lp_write(void *u, u16 a, u8 d) {
		auto *m = static_cast<tek8301 *>(u); m->bp_check(a, true);
		if (m->m_rtpa_watch) m->rtpa_board.bus(a, d, false, true, false);
		if (m->m_tta_watch) m->tta_board.target_bus(m->ep.cyc, a, d, tta::MWRITE);
		m->slave_wr(a, d);
	}
	bool m_rtpa_watch = false;     // the emulator processor is stepping: the RTPA sees its cycles
	bool m_tta_watch = false;      // ... and the TTA, in run mode (emulator processor only)
	u8   slave_rd(u16 a, bool m1 = false);   // m1: opcode fetch
	void slave_wr(u16 a, u8 d);
	// Memory map / write-protect RAM: 512 blocks x 128 bytes, D0 = U (prototype), D2 = write
	// protect. The 2650 reaches it at system FE00-FFFF while MAPEN (EE<3>) is set:
	// block = (addr<7:0> << 1) | addr<8>  (DOS/50 writes even blocks at FExx, odd at FFxx).
	u8 m_map[512] = {};
	bool m_wp_hit = false;
	bool m_nmi_prev = false;
	prototype_bus m_no_target;
	bool m_trst_prev = false;   // v3 target RESET edge
	static u8 lp_in(z80 *z, u16 port);     // full 16-bit address (A8-A15 from A or B)
	static void lp_out(z80 *z, u16 port, u8 d);

	// HSI UART
	u8 m_hsi_rbr = 0; bool m_hsi_da = false, m_hsi_oe = false; bool m_hsi_tbre = true;
	int m_hsi_tx_timer = 0, m_hsi_rx_gap = 0; u8 m_hsi_thr = 0;
	std::deque<u8> m_hsi_rxq;
	i8257 m_dma;
	void hsi_tick(int c);
	void dma_service();

	// memory
	struct busaddr { u32 a; bool sys; bool dma = false; };
	busaddr cpu_to_bus(offs_t a, bool write);
	u8 mem_rd(busaddr b, bool ovl = true);
	void mem_wr(busaddr b, u8 d);
	u8 sys_rd(u16 a) { return m_sysram[a]; }
	void update_irq();
	u32 level_irqs() const;
};
#endif
