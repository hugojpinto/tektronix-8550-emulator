// Motorola MC6850 ACIA (programmer's model), host side as byte queues.
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
		if (!rs) return status();
		m_status &= ~(RDRF | OVRN);
		return m_rdr;
	}
	void write(int rs, u8 d)
	{
		if (!rs) {
			m_cr = d;
			if ((d & 3) == 3) { m_reset = true; m_status = 0; m_tx_timer = 0; }
			else if (m_reset) { m_reset = false; m_status |= TDRE; }
		} else if (!m_reset) {
			m_tdr = d; m_status &= ~TDRE; m_tx_timer = char_cycles;
		}
	}
	u8 status() const
	{
		u8 s = m_status;
		if (irq()) s |= IRQ;
		return s;
	}
	bool irq() const
	{
		if (m_reset) return false;
		bool rie = m_cr & 0x80;
		bool tie = ((m_cr >> 5) & 3) == 1;
		return (rie && (m_status & (RDRF | OVRN))) || (tie && (m_status & TDRE));
	}
	void tick(int cycles)
	{
		if (m_reset) return;
		if (m_tx_timer > 0 && (m_tx_timer -= cycles) <= 0) {
			m_tx_timer = 0; m_status |= TDRE;
			if (on_tx) on_tx(m_tdr);
		}
		if (m_rx_gap > 0) m_rx_gap -= cycles;
		if (!m_rxq.empty() && m_rx_gap <= 0) {
			if (m_status & RDRF) { if (!pace_rx) { m_status |= OVRN; } else return; }
			m_rdr = m_rxq.front(); m_rxq.pop_front();
			m_status |= RDRF; m_rx_gap = char_cycles;
		}
	}
	void host_rx(u8 c) { m_rxq.push_back(c); }
	std::function<void(u8)> on_tx;
	int  char_cycles = 2000;   // ~1 ms of 2 MHz 2650 clock (9600 baud)
	bool pace_rx = true;       // host input waits for the program to read (no overruns)

private:
	u8 m_cr = 0, m_status = 0, m_rdr = 0, m_tdr = 0;
	bool m_reset = true;
	int m_tx_timer = 0, m_rx_gap = 0;
	std::deque<u8> m_rxq;
};
#endif
