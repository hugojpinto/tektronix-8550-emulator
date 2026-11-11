// 8550 Real-Time Prototype Analyzer (RTPA) option on the 8301: ports C0-C7.
// Reverse-engineered from the diagnostic RTPA.TST V1.2 (8550_DIAG.IMD) and DOS/50;
// see notes/rtpa.md for the evidence. The board watches the emulator processor's
// bus: two event comparators, two 8253 counter/timers (one per event channel: pass
// count, general-purpose counter, delay), sequencing (pass, delay, arm, limits),
// PC latches, and the debug interrupt INT 29 that stops the emulator processor.
#ifndef TEK_RTPA_H
#define TEK_RTPA_H
#include "emu.h"
#include <cstdio>
#include <cstdlib>

class rtpa
{
public:
	bool present = false;
	bool trace = getenv("TEK_RTPATRACE") != nullptr;
	double *clock = nullptr;                  // seconds, for the trace

	u8 read(int r);
	void write(int r, u8 d);

	// emulator-processor bus, called by the 8301 for every cycle of the emulator processor
	void instr_begin(u16 pc);
	void bus(u16 a, u8 d, bool fetch, bool write, bool io);
	void instr_end(u16 next_pc, int tstates = 0, double hz = 4e6);
	bool stopped() const { return m_stopped; }   // holds the emulator processor (SLV OPREQ)
	void release() { m_stopped = false; }        // the Emulator Controller takes over (F9 command)
	bool take_irq() { bool i = m_irq; m_irq = false; return i; }   // INT 29

private:
	// ---- 8253 programmable interval timer (one per event channel)
	struct counter {
		u16 cr = 0, ce = 0;
		u8 mode = 0, rl = 3;
		bool pending = false, wr_msb = false, rd_msb = false, out = false;
		void write(u8 d);
		u8 read();
		bool clock();                         // true on a rising edge of OUT
	};
	struct pit { counter c[3]; void control(u8 d); };
	pit m_pit[2];

	// ---- event comparators: C4 RAM (8 bytes per event, index = 2*field + event)
	u8 m_ram[16] = {};                        // as written (some fields inverted)
	u8 m_mode[4] = {};                        // C7: [0/1] address compare, [2/3] data compare
	u8 m_c0 = 0, m_c1 = 0, m_c2 = 0, m_c3 = 0, m_index = 0;
	u8 m_status = 0;                          // 01 event 1, 02 event 2, 08 stopped
	u16 m_pc_last = 0, m_pc_next = 0, m_cur_pc = 0;
	bool m_armed = false, m_delay[2] = {}, m_trig = false, m_stopped = false, m_irq = false;
	double m_us_frac = 0;                     // time base for CNT usec/msec
	u16 m_prescale = 0;

	// ---- real-time trace buffer: the last 128 qualified transactions (C1 = RTT qualifier)
	static constexpr int TRACE = 128;
	struct entry { u8 hi, lo, bus, ext, data, probe; };
	entry m_trace[TRACE] = {};
	int m_tr_head = 0, m_tr_count = 0, m_tr_rd = 0, m_tr_byte = 0;
	bool traced(u8 cyc) const;
	u8 trace_read();

	bool match(int e, u16 a, u8 d, u8 cyc) const;
	void occurrence(int e, bool &hit);
	void trigger(int e) { m_status |= u8(1 << e); m_trig = true; }
	void clock_gp(bool gate) { if (gate) m_pit[0].c[1].clock(); }
	bool gp_gate() const { return (m_c2 & 0x04) ? m_armed : true; }   // ARM: G.P. counts once armed
	int gp_source() const { return m_c3 & 7; }   // CNT: 0 evt1 1 evt2 2 trce 3 ftch 4 cycl 5 emul 6 msec 7 usec
	void log(const char *what, int r, u8 v) { if (trace) fprintf(stderr, "[%.4f RTPA %s C%d %02x]\n", clock ? *clock : 0.0, what, r, v); }
};
#endif
