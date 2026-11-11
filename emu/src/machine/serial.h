// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// DL11-style serial line unit as found on the 8501 Utility board
// (RCSR/RBUF/XCSR/XBUF at base+0/2/4/6, receive vector V, transmit vector V+4).
#ifndef TEK_SERIAL_H
#define TEK_SERIAL_H
#include "emu.h"
#include <deque>
#include <functional>

class serial_port
{
public:
	serial_port(u16 base, u16 vector) : m_base(base), m_vector(vector) {}

	u16 base() const { return m_base; }
	bool decodes(offs_t a) const { return (a & 0177770) == m_base; }

	u16 read(offs_t a)
	{
		switch (a & 6) {
		case 0: return m_rcsr;
		case 2: { u16 v = m_rbuf; m_rcsr &= ~0200; return v; }
		case 4: return m_xcsr;
		default: return 0;
		}
	}
	void write(offs_t a, u16 d, bool byte_hi = false)
	{
		switch (a & 6) {
		case 0:
			if (!byte_hi) {
				bool was = m_rcsr & 0100;
				m_rcsr = (m_rcsr & ~0101) | (d & 0101);
				if (!was && (m_rcsr & 0100) && (m_rcsr & 0200)) m_rx_irq = true;
				if (!(m_rcsr & 0100)) m_rx_irq = false;
			}
			break;
		case 2: break;
		case 4: {
			bool was = m_xcsr & 0100;
			if (byte_hi) m_xcsr = (m_xcsr & 0x0fff) | (d & 0xf000);
			else m_xcsr = (m_xcsr & ~0141) | (d & 0141);
			if (!was && (m_xcsr & 0100) && (m_xcsr & 0200)) m_tx_irq = true;
			if (!(m_xcsr & 0100)) m_tx_irq = false;
			break;
		}
		case 6:
			if (!byte_hi) {
				m_xcsr &= ~0200; m_tx_irq = false;
				m_tx_char = u8(d); m_tx_timer = char_cycles;
			}
			break;
		}
	}
	void reset() { m_rcsr = 0; m_xcsr = 0200; m_rx_irq = m_tx_irq = false; m_tx_timer = 0; }

	// time passes
	void tick(int cycles)
	{
		if (m_tx_timer > 0 && (m_tx_timer -= cycles) <= 0) {
			m_tx_timer = 0;
			if (on_tx) on_tx(m_tx_char);
			m_xcsr |= 0200;
			if (m_xcsr & 0100) m_tx_irq = true;
		}
		if (m_rx_gap > 0) m_rx_gap -= cycles;
		if (!(m_rcsr & 0200) && !m_rxq.empty() && m_rx_gap <= 0) {
			m_rbuf = m_rxq.front(); m_rxq.pop_front();
			m_rcsr |= 0200; m_rx_gap = char_cycles;
			if (m_rcsr & 0100) m_rx_irq = true;
		}
	}

	// host side
	void host_rx(u8 c) { m_rxq.push_back(c); }
	bool host_rx_empty() const { return m_rxq.empty(); }
	void host_tx_direct(u8 c) { if (on_tx) on_tx(c); }
	std::function<void(u8)> on_tx;

	// interrupts: returns vector and clears the request, or -1
	bool irq() const { return m_rx_irq || m_tx_irq; }
	int ack() {
		if (m_rx_irq) { m_rx_irq = false; return m_vector; }
		if (m_tx_irq) { m_tx_irq = false; return m_vector + 4; }
		return -1;
	}

	int char_cycles = 4700;   // ~1.04 ms (9600 baud) at tek8501::CPU_HZ

private:
	u16 m_base, m_vector;
	u16 m_rcsr = 0, m_rbuf = 0, m_xcsr = 0200;
	bool m_rx_irq = false, m_tx_irq = false;
	int m_tx_timer = 0, m_rx_gap = 0;
	u8 m_tx_char = 0;
	std::deque<u8> m_rxq;
};
#endif
