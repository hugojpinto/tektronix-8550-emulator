// Tektronix 8501 Flexible Disc Controller (board A50, 670-6228-00): a Z80A at 4 MHz
// running the 4 KB ROM 160-0412-00, 1 KB of RAM, TTL drive interface (FM/MFM
// serialiser, mark detection, CRC) and a Q-bus DMA interface. The LSI-11 sees one
// register, CBPR (177160); everything else goes through DMA to the command block.
// Register map and behaviour: notes/8501-fdc.md.
#ifndef TEK_MACHINE_TEK8501_FDC_H
#define TEK_MACHINE_TEK8501_FDC_H
#include "emu.h"
#include "machine/flexdrive.h"
extern "C" {
#include "cpu/z80/superzazu/z80.h"
}
#include <functional>
#include <string>

class tek8501_fdc
{
public:
	tek8501_fdc();
	bool load_rom(const std::string &path, std::string &err);
	void reset();                         // power-up / BINIT
	void run_until(u64 cycle);            // advance to a time in controller cycles (4 MHz)
	u64 cycles() const { return m_cycles; }

	// LSI-11 side
	void cbpr_write(u16 v);
	u16  cbpr_read() const { return m_cbpr; }
	bool irq() const { return m_qirq; }
	u16  iack() { m_qirq = false; return m_dtr; }   // the vector comes from DTR
	// Q-bus DMA, supplied by the host: false = no response (non-existent memory)
	std::function<bool(u32 addr, u16 &v)> dma_read;
	std::function<bool(u32 addr, u16 v)> dma_write;

	flex::drive drv[4];
	int  selected() const;                // -1 = none
	bool drive_lamp(int d) const { return selected() == d && (m_hcr0 & 0x04); }
	bool trace = false;

private:
	z80 z;
	u8 m_rom[4096], m_ram[1024];
	u64 m_cycles = 0, m_stall_to = 0;
	// Q-bus interface
	u16 m_cbpr = 0, m_dtr = 0, m_drr = 0, m_dmaar = 0;
	bool m_busy = false, m_multint = false, m_qirq = false;
	// drive interface
	u8 m_hcr0 = 0, m_hcr1 = 0;
	bool m_crc_err = false, m_dma_timeout = false, m_no_data = false, m_not_ready_rw = false;
	// read logic
	bool m_rd_armed = false; u64 m_rd_arm_time = 0; s64 m_rd_next = -1; u16 m_rd_crc = 0;
	// write logic: the byte in the shift register is committed when the next write
	// (whose address carries the control bits for it) arrives, or when it runs out
	bool m_wr_active = false; u64 m_wr_abs = 0; u8 m_wr_byte = 0; u16 m_wr_crc = 0; int m_wr_crc_phase = 0;

	bool dd() const { return m_hcr1 & 0x02; }
	u64  cc() const { return flex::cell_cycles(dd()); }
	u64  now() const { return std::max<u64>(z.cyc, m_stall_to); }
	void stall(u64 t) { if (t > now()) m_stall_to = t; }
	flex::drive *sel();
	flex::track *cur_track();
	u8   hsr0();
	u8   hsr1() const { return u8((m_dma_timeout ? 0x80 : 0) | (m_no_data ? 0x40 : 0) | (m_not_ready_rw ? 0x20 : 0) | 0x01); }
	bool int_line() const { return m_dma_timeout || m_no_data || m_not_ready_rw; }
	void hcr0_write(u8 v);
	u8   read_sync();
	u8   read_next(bool with_wait);
	void arm();
	void dwdr_write(u16 a, u8 v);
	void wr_commit(u16 ctl);
	void wr_settle();
	void dma_start();
	u8   mem_read(u16 a);
	void mem_write(u16 a, u8 v);
	static u8 rd(void *, u16);
	static void wr(void *, u16, u8);
	static u8 in(z80 *, u16);
	static void out(z80 *, u16, u8);
};
#endif
