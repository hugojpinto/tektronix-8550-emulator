// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Motorola MC6850 ACIA (programmer's model), host side as byte queues.
// The transmitter is double-buffered, as on the chip: a byte written while the shift
// register is idle moves into it at once and TDRE stays set, so a second byte can wait in
// the data register while the first is sent. (Found with the 8002's TEKDOS, whose printer
// driver writes ahead and lost every other byte with a single buffer; DOS/50 waits for
// TDRE before every byte, so on the 8550 it only made output slower than the chip's.)
// The same file serves the 8550 and 8002 emulators.
// Modem lines (MC6850 data sheet), for what is plugged into the port: RTS out (high only with
// control bits 6-5 = 10); CTS in, which while high inhibits TDRE; DCD in, whose low-to-high
// edge latches status bit 2 (and interrupts, with receive interrupts on) until a status read
// followed by a data read, and which while high holds the receiver in reset. Break (control
// bits 6-5 = 11) holds the line spacing. Left unconnected, CTS and DCD stay low and nothing
// changes. (Found with the sibling 8002 emulator, its diagnostics' SYS COMM test and its wrap-back fixture.)
#ifndef TEK_MC6850_H
#define TEK_MC6850_H
#include "emu.h"
#include <deque>
#include <functional>

class mc6850
{
public:
	enum { RDRF = 0x01, TDRE = 0x02, DCD = 0x04, CTS = 0x08, FE = 0x10, OVRN = 0x20, PE = 0x40, IRQ = 0x80 };

	u8 read(int rs)
	{
		if (!rs) { m_dcd_read = (m_status & DCD) != 0; return status(); }
		m_status &= ~(RDRF | OVRN);
		// an overrun shows once the character before it has been read (data sheet), with RDRF
		if (m_ovrn_pending) { m_ovrn_pending = false; m_status |= OVRN | RDRF; }
		if (m_dcd_read) { m_dcd_read = false; if (!m_dcd) m_status &= ~DCD; m_dcd_irq = false; }
		return m_rdr;
	}
	void write(int rs, u8 d)
	{
		if (!rs) {
			m_cr = d;
			if ((d & 3) == 3) { m_reset = true; m_status = 0; m_tx_timer = 0; m_tdr_full = false; m_dcd_irq = false; m_ovrn_pending = false; }
			else if (m_reset) { m_reset = false; m_status |= TDRE; }
			if (clock_hz > 0 && (d & 3) != 3) {
				// a character: start bit, data, parity, stop bits (word select, bits 4-2) at the
				// clock over the counter divide ratio (bits 1-0: 1, 16, 64)
				static const int frame[8] = { 11, 11, 10, 10, 11, 10, 11, 11 };
				static const int divide[3] = { 1, 16, 64 };
				char_cycles = int(double(frame[(d >> 2) & 7]) * divide[d & 3] * cpu_hz / clock_hz + 0.5);
			}
			if (on_control) on_control();   // what is plugged in sees RTS and break at once
		} else if (!m_reset) {
			if (m_tx_timer == 0) { m_shift = d; m_tx_timer = char_cycles; m_status |= TDRE; }   // straight into the shift register
			else { m_tdr = d; m_tdr_full = true; m_status &= ~TDRE; }
		}
	}
	u8 status() const
	{
		u8 s = m_status;
		if (m_cts) s = u8((s & ~TDRE) | CTS);
		if (irq()) s |= IRQ;
		return s;
	}
	bool irq() const
	{
		if (m_reset) return false;
		bool rie = m_cr & 0x80;
		bool tie = ((m_cr >> 5) & 3) == 1;
		return (rie && ((m_status & (RDRF | OVRN)) || m_dcd_irq)) || (tie && (m_status & TDRE) && !m_cts);
	}
	bool rts() const { return ((m_cr >> 5) & 3) == 2; }        // the RTS pin is high (negated)
	bool sending_break() const { return !m_reset && ((m_cr >> 5) & 3) == 3; }
	void set_cts(bool high) { m_cts = high; }
	void set_dcd(bool high)
	{
		if (high && !m_dcd && !m_reset) { m_status |= DCD; m_dcd_irq = true; }
		if (!high && !m_dcd_irq && !m_dcd_read) m_status &= ~DCD;
		m_dcd = high;
	}
	void tick(int cycles)
	{
		if (m_reset) return;
		if (m_tx_timer > 0 && (m_tx_timer -= cycles) <= 0) {
			m_tx_timer = 0;
			if (on_tx) on_tx(m_shift);
			if (m_tdr_full) { m_shift = m_tdr; m_tdr_full = false; m_tx_timer = char_cycles; }
			m_status |= TDRE;
		}
		if (m_rx_gap > 0) m_rx_gap -= cycles;
		if (m_dcd) { m_status &= ~(RDRF | OVRN | FE); return; }   // the receiver held in reset
		if (!m_rxq.empty() && m_rx_gap <= 0) {
			if (m_status & RDRF) {
				if (pace_rx) return;
				m_rxq.pop_front(); m_ovrn_pending = true; m_rx_gap = char_cycles;   // lost
				return;
			}
			u16 r = m_rxq.front(); m_rxq.pop_front();
			m_rdr = u8(r);
			// a BREAK arrives as a null character with a framing error (no stop bit)
			m_status = u8((m_status & ~FE) | RDRF | ((r & 0x100) ? FE : 0)); m_rx_gap = char_cycles;
		}
	}
	void host_rx(u8 c) { m_rxq.push_back(c); }
	void host_break() { m_rxq.push_back(0x100); }   // the line held at spacing (BREAK key)
	std::function<void(u8)> on_tx;
	std::function<void()> on_control;   // after each control write (RTS, break)
	int  char_cycles = 2000;   // ~1 ms of 2 MHz 2650 clock (9600 baud)
	double clock_hz = 0, cpu_hz = 0;   // when set: char_cycles follows the control register
	bool pace_rx = true;       // host input waits for the program to read (no overruns)

private:
	u8 m_cr = 0, m_status = 0, m_rdr = 0, m_tdr = 0, m_shift = 0;
	bool m_tdr_full = false;
	bool m_reset = true;
	bool m_cts = false, m_dcd = false, m_dcd_irq = false, m_dcd_read = false, m_ovrn_pending = false;
	int m_tx_timer = 0, m_rx_gap = 0;
	std::deque<u16> m_rxq;   // bit 8: break
};
#endif
