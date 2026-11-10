// Tektronix 8301 Microprocessor Development Unit (2650 system processor),
// part of the 8550 MDL. Boards modelled:
//   System Controller : 2650, boot/diag ROM overlay (EA), bank switch (EE),
//                       3x MC6850, IM6402 HSI UART + 8257 DMA, interval timer,
//                       LEDs/mode switch, interrupt encoder
//   System RAM        : 64 KB, paging of 2000-3FFF (D3), refresh/parity diag (D2)
//   Program Memory    : 64 KB (two 32 KB boards, relocation regs B8/B9 stored)
// Not yet: Emulator Controller (F0-FF), Language Processor (E7), memory map/WP.
#ifndef TEK_TEK8301_H
#define TEK_TEK8301_H
#include "emu.h"
#include "cpu/s2650/s2650.h"
#include "machine/mc6850.h"
#include "machine/i8257.h"
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

	u8 peek(offs_t a) { return mem_rd(cpu_to_bus(a & 0x7fff, false), false); }

private:
	std::vector<u8> m_boot, m_diag, m_sysram, m_progmem;
	std::vector<u8> m_par;        // per-byte: parity-complement state when written
	u16 m_par_addr = 0;
	u8 m_ea = 0, m_ee = 0, m_d2 = 0, m_d3 = 0, m_e9 = 0, m_ec = 0, m_led = 0, m_e7 = 0;
	u8 m_b8 = 0, m_b9 = 0, m_eb_out = 0;
	u64 m_cycles = 0, m_next_timer = 0, m_next_refresh = 0;
	u32 m_pending = 0;            // latched interrupt flip-flops (bit n = int n)

	// HSI UART
	u8 m_hsi_rbr = 0; bool m_hsi_da = false, m_hsi_oe = false; bool m_hsi_tbre = true;
	int m_hsi_tx_timer = 0, m_hsi_rx_gap = 0; u8 m_hsi_thr = 0;
	std::deque<u8> m_hsi_rxq;
	i8257 m_dma;
	void hsi_tick(int c);
	void dma_service();

	// memory
	struct busaddr { u32 a; bool sys; };
	busaddr cpu_to_bus(offs_t a, bool write);
	u8 mem_rd(busaddr b, bool ovl = true);
	void mem_wr(busaddr b, u8 d);
	u8 sys_rd(u16 a) { return m_sysram[a]; }
	void update_irq();
	u32 level_irqs() const;
};
#endif
