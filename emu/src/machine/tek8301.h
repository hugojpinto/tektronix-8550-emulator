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
extern "C" {
#include "cpu/z80/superzazu/z80.h"
}
#include <vector>
#include <deque>
#include <string>
#include <functional>

class tek8301 : public s2650_device::bus_interface
{
public:
	static constexpr double CPU_HZ = 2'000'000.0;   // FAST clock (J5704)

	tek8301();
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
	z80 lp;
	// slave runs while the master is halted and no debug interrupt is outstanding
	bool lp_running() const { return (m_e7 & 0x80) && cpu.halted() && !lp.halted && m_lp_live && !m_ec_pend && !(m_pending & 0x00ff0000u); }
	u16 lp_pc_last() const { return m_lp_pc_last; }
	u8 progmem(u16 a) const { return m_progmem[a]; }
	void progmem_w(u16 a, u8 d) { m_progmem[a] = d; }
	std::function<void(u16)> lp_trace_hook;
	std::string lp_state() const {
		return util::string_format("LP: e7=%02x live=%d z80halt=%d pc=%04x ec_pend=%02x f8=%02x f9=%02x pend=%08x 2650halt=%d",
			m_e7, m_lp_live, int(lp.halted), lp.pc, m_ec_pend, m_f8, m_f9, m_pending, int(cpu.halted()));
	}

	u8 peek(offs_t a) { return mem_rd(cpu_to_bus(a & 0x7fff, false), false); }

private:
	std::vector<u8> m_boot, m_diag, m_sysram, m_progmem;
	std::vector<u8> m_par;        // per-byte: parity-complement state when written
	u16 m_par_addr = 0;
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
	u8 prog_rd(u8 ext, u16 a) const { return prog_present(ext, a) ? m_progmem[a] : 0xff; }
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
	void lp_run(int cycles2650);
	void ec_interrupt(int n);     // n = 16..31
	static u8 lp_read(void *u, u16 a) { auto *m = static_cast<tek8301 *>(u); m->bp_check(a, false); return m->prog_rd(PROG_BANK, a); }
	static void lp_write(void *u, u16 a, u8 d) { auto *m = static_cast<tek8301 *>(u); m->bp_check(a, true); m->prog_wr(PROG_BANK, a, d); }
	static u8 lp_in(z80 *z, u8 port);
	static void lp_out(z80 *z, u8 port, u8 d);

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
