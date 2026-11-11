// 8550 Trigger Trace Analyzer -- see tta.h and notes/tta.md.
#include "machine/tta.h"

u8 tta::read(int r)
{
	u8 v;
	switch (r) {
	case 0: v = u8(m_reg[0] | 0xc0); break;           // bits 7-6 read high: the board is there (module 00)
	case 1: case 2: case 4: v = m_reg[r]; break;
	case 3: v = u8(m_reg[3] & 0x0f); break;           // 4 bits
	case 6: v = stc.read8(0); break;                  // Am9513 data
	case 7: v = stc.read8(1); break;                  // Am9513 status
	default: v = 0xff; break;
	}
	log("rd", r, v);
	return v;
}

void tta::write(int r, u8 d)
{
	log("wr", r, d);
	switch (r) {
	case 6: stc.write8(0, d); break;
	case 7: stc.write8(1, d); break;
	default: m_reg[r] = d; break;
	}
}

u8 tta::ram_read(u16 a)
{
	if ((a >> 8) == 0xf5) return 0xff;                // no RAM in page F5: system RAM is inhibited there too
	return m_ram[a - 0xe800];
}

void tta::ram_write(u16 a, u8 d)
{
	if ((a >> 8) == 0xf5) return;
	m_ram[a - 0xe800] = d;
}
