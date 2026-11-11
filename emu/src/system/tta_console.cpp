// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA command set -- see tta_console.h. Syntax, parameters and displays follow the
// Trigger Trace Analyzer users manual 070-3760-01, section 2 (and the 8540 OS/40 manual's
// summary of the same commands where the TTA manual is silent). How the commands program
// the board follows OS/40's TTA modules (EVE, COU, BRE, CONS, ACQ, TCLR, DISP and the
// resident TTA2 at 72F0); addresses in the comments are theirs (notes/tta-os40.md).
#include "system/tta_console.h"
#include "system/tek8550.h"
#include "system/tta_eve.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <sstream>
#include <vector>

namespace {
using C = tta_console;

std::string lower(std::string s) { for (char &c : s) c = char(tolower(u8(c))); return s; }
std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...)
{
	char b[256]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); return b;
}
struct error { std::string what; };

// A number as the TTA commands take it (§2, "Numbers"): a default radix, overridden by a
// suffix (H hex, T decimal, Y binary); X for a don't-care digit (a nibble in hex, a bit in
// binary). care gets 1s for every bit that matters, over `bits`.
struct number { u32 v = 0, care = 0; bool masked = false; };
number parse_number(const std::string &tok, int radix, int bits)
{
	std::string s = tok;
	if (s.empty()) throw error{ "missing value" };
	char suf = char(toupper(u8(s.back())));
	if (suf == 'H' && s.size() > 1) { radix = 16; s.pop_back(); }
	else if (suf == 'T' && s.size() > 1) { radix = 10; s.pop_back(); }
	else if (suf == 'Y' && s.size() > 1) { radix = 2; s.pop_back(); }
	const u32 full = bits >= 32 ? 0xffffffffu : ((1u << bits) - 1);
	number n; n.care = full;
	if (radix == 10) {
		for (char c : s) { if (!isdigit(u8(c))) throw error{ "bad number " + tok }; n.v = n.v * 10 + u32(c - '0'); }
		n.v &= full; return n;
	}
	const int per = radix == 16 ? 4 : 1;
	u32 xmask = 0;
	for (char c : s) {
		int d;
		if (c == 'x' || c == 'X') d = -1;
		else if (isdigit(u8(c))) d = c - '0';
		else if (radix == 16 && isxdigit(u8(c))) d = 10 + toupper(u8(c)) - 'A';
		else throw error{ "bad number " + tok };
		if (d >= radix) throw error{ "bad number " + tok };
		n.v = (n.v << per) | u32(d < 0 ? 0 : d);
		xmask = (xmask << per) | (d < 0 ? ((1u << per) - 1) : 0);
	}
	n.v &= full; xmask &= full;
	if (xmask) { n.masked = true; n.care = full & ~xmask; }
	return n;
}

int channel(const std::string &t)
{
	if (t.size() == 1 && t[0] >= '1' && t[0] <= '4') return t[0] - '1';
	return -1;
}
bool is_all(const std::string &t) { return t == "all" || t == "a"; }

const char *src_name(C::src_t s)
{
	static const char *n[] = { "---", "200NSEC", "2USEC", "20USEC", "200USEC", "2MSEC", "EV1", "EV2", "EV3", "EV4",
		"TRIG1", "TRIG2", "TRIG3", "TRIG4", "ACQ", "CYC", "EMUCLK", "QUA" };
	return n[s];
}
C::src_t parse_src(const std::string &t0)
{
	std::string t = lower(t0);
	if (t.compare(0, 2, "s=") == 0) t = t.substr(2);
	for (int i = C::S_200NS; i <= C::S_QUA; i++) if (lower(src_name(C::src_t(i))) == t) return C::src_t(i);
	if (t == "evl") return C::S_EV1;                // as printed in the manual's examples
	throw error{ "bad source " + t0 };
}
const char *out_name(C::out_t o) { static const char *n[] = { "---", "ARM", "DISARM", "PULSE", "DELAY", "TIMEOUT" }; return n[o]; }
const char *gate_name(C::gate_t g) { static const char *n[] = { "---", "OFF", "CTR", "TRIGH", "TRIGL", "SEQH", "SEQL", "SELF" }; return n[g]; }

// bus symbols (the 8085A set the manual uses; the Z80 board takes the same ones)
u8 parse_bus(const std::string &t)
{
	std::string s = lower(t);
	if (s == "f") return C::B_F;
	if (s == "nf") return C::B_NF;
	if (s == "m") return C::B_M;
	if (s == "i") return C::B_I;
	if (s == "rd") return C::B_RD;
	if (s == "wt") return C::B_WT;
	throw error{ "bad bus symbol " + t };
}
std::string bus_text(u8 b)
{
	std::string s;
	auto add = [&](u8 m, const char *n) { if (b & m) { if (!s.empty()) s += ' '; s += n; } };
	add(C::B_M, "M"); add(C::B_I, "I"); add(C::B_RD, "RD"); add(C::B_WT, "WT"); add(C::B_F, "F"); add(C::B_NF, "NF");
	return s;
}
// the bus symbols as (mask, value) pairs on the bus status byte: the 8085 support's table
// (DOS/50 V2.1 EMUMON1: NF 01,00  M 04,04  I 04,00  RD 02,02  WT 02,00  F 01,01)
void bus_pairs(u8 b, u8 &mask, u8 &val)
{
	static const struct { u8 sym, mask, val; } t[] = { { C::B_NF, 1, 0 }, { C::B_M, 4, 4 }, { C::B_I, 4, 0 }, { C::B_RD, 2, 2 }, { C::B_WT, 2, 0 }, { C::B_F, 1, 1 } };
	mask = val = 0;
	for (auto &e : t) if (b & e.sym) { mask |= e.mask; val |= e.val; }
}

}

tta_console::tta_console(tek8550_system &s) : sys(s)
{
	tek8301 &m = sys.mdu_unit();
	m.tta_sw_go = [this](bool a) { go(a); };
	m.tta_sw_stop = [this]() { stop(); };
	m.tta_sw_break = [this](u8 trig, u16 pc) { return on_break(trig, pc); };
	init();
}

tta &tta_console::board() const { return sys.mdu_unit().tta_board; }
u8 tta_console::port(int r) const { return board().port_read(r); }
void tta_console::port_w(int r, u8 d) const { board().port_write(r, d); }
// the comparator and acquisition RAMs, through the system-bus window (port 70 bit 1), as
// OS/40's move service reaches them
void tta_console::ram_w(int page, int row, u8 d) const { board().ram_write(u16((0xe8 + page - 0x68) << 8 | row), d); }
u8 tta_console::ram(int page, int row) const { return board().ram_read(u16((0xe8 + page - 0x68) << 8 | (row & 0xff))); }

// The software's start: the counters reset (COU 24A6: master mode with BCD scaling for the
// timebases, every counter ungated, counting F1 up, output high impedance) and every
// channel cleared (TCLR -x), acquiring all cycles
void tta_console::init()
{
	port_w(7, 0xff);                              // master reset
	port_w(7, 0x17); port_w(6, 0x00); port_w(6, 0x80);   // master mode: BCD scaling (F2-F5 = F1 / 10^n)
	port_w(7, 0xe7);                              // 8-bit bus
	port_w(0, 0x00); port_w(3, 0x00);
	clear_all();
}

// ---- events: EVE writes every comparator RAM row for event k (a cleared part is "always true")
void tta_console::apply_event(int k)
{
	const event_def &e = m_ev[k];
	tta_eve v;
	v.addr = e.addr; v.data = e.data;
	bus_pairs(e.bus, v.ym, v.yv);
	v.xm = u8(e.ctr_care & 0x0f); v.xv = u8(e.ctr_val & e.ctr_care & 0x0f);
	if (e.qual >= 0) { v.xm |= 0x10; v.xv |= u8(e.qual << 4); }
	v.pm = e.probe_care; v.pv = u8(e.probe_val & e.probe_care);
	// the Z80 and 8085 supports' widths: 16-bit addresses (40B4 = 00), an 8-bit data bus
	v.top_mask = 0x00; v.data_wide = false;
	port_w(0, u8(port(0) | 0x02));
	tta_eve_write(k, v, [this](int p, int r) { return ram(p, r); }, [this](int p, int r, u8 d) { ram_w(p, r, d); });
	port_w(0, u8(port(0) & ~0x02));
}

// ---- counters (COU): the block, then OS/40's Am9513 sequence (TTA2 7333) for counter c
void tta_console::apply_counter(int c, bool now)
{
	block &b = m_blk[c];
	if (c < 4) {
		const counter &k = m_ctr[c];
		static const u8 type[] = { 0, 0, 1, 4, 3, 2 };            // O_NONE ARM DISARM PULSE DELAY TIMEOUT
		static const u8 lo[] = { 0x0c, 0x0c, 0x08, 0x01, 0x02, 0x02 };
		static const u8 gating[] = { 0, 0, 1, 4, 5, 6, 7, 2 };     // NONE OFF CTR TRIGH TRIGL SEQH SEQL SELF
		static const u8 srcc[] = { 0xb, 0xb, 0xc, 0xd, 0xe, 0xf, 1, 2, 3, 4, 7, 8, 9, 0xa, 6, 5, 5, 5 };
		b.type = k.prog ? type[k.mode] : 0;
		b.mode = k.prog ? u16((gating[k.gate] << 13) | (srcc[k.src] << 8) | lo[k.mode] | (k.restart == 1 ? 0x80 : 0)) : 0x0b0c;
		b.load = b.hold = k.prog && k.has_value ? u16(k.init) : 0;
	}
	m_reprogram |= u8(1 << c);
	src5_select();
	if (now) { port_w(7, u8(0xc0 | (1 << c))); counter_sequence(c); }
}

// TTA2 7333-740C for one counter: mode, load and hold registers; load and arm; then the reload
// value and the starting toggle that make the output follow o= (notes/tta-os40.md §3)
void tta_console::counter_sequence(int c)
{
	const block &b = m_blk[c];
	const int n = c + 1;
	const bool restart = b.mode & 0x80;
	const int lo = b.load & 0xff, hi = b.load >> 8;
	auto cmd = [&](u8 v) { port_w(7, v); };
	auto data = [&](u8 v) { port_w(6, v); };
	cmd(0xe0); cmd(u8(n));                          // data pointer sequencing on: mode, load, hold
	data(u8(b.mode)); data(u8(b.mode >> 8)); data(u8(lo)); data(u8(hi)); data(u8(b.hold)); data(u8(b.hold >> 8));
	cmd(0xe8);
	cmd(u8(0x08 | n));                              // the load register
	if (b.type >= 2) {
		if (hi == 0 && lo < 3) { data(0x02); data(0x00); }
		else {
			const int sub = restart && lo > 3 ? 2 : 1;
			data(u8(lo - sub)); data(u8(lo >= sub ? hi : hi - 1));   // v-1 (v-2 with restart)
		}
	}
	cmd(u8(0x60 | (1 << c))); cmd(u8(0x60 | (1 << c)));   // load and arm
	cmd(u8(0x08 | n));
	if (b.type <= 1) {
		if (!restart) { data(0xfe); data(0xff); }   // reload FFFE: up-counters stop near the top
	} else if (!restart) {
		auto zero = [&]() { cmd(u8(0x08 | n)); data(0); data(0); };
		auto step = [&]() { cmd(u8(0xf0 | n)); };
		if (hi != 0 || lo > 2) zero();
		else if (lo == 1) { zero(); step(); if (!(b.mode & 0xe000)) step(); }
		else { step(); zero(); }
	}
	if (b.type == 2) cmd(u8(0xe8 | n));             // timeout: output high until the count is done
	else if (b.type == 3) cmd(u8(0xe0 | n));        // delay: low until then
}

// SRC5 (port 70 bits 5-4): 00 bus cycles, 11 the emulator clock (01 where the emulator asks),
// 10 the qualifier (COU 2419, ACQ 223C)
void tta_console::src5_select()
{
	u8 sel = 0;
	auto uses = [&](src_t s) {
		if (s == S_EMUCLK) sel = 0x30; else if (s == S_QUA) sel = 0x20; else if (s == S_CYC) sel = 0x00;
	};
	for (const counter &k : m_ctr) if (k.prog) uses(k.src);
	if (m_acq_for) uses(m_acq_src);
	port_w(0, u8((port(0) & 0xcf) | sel));
}

// ---- breaks (BRE): port 72 bits 3-0 enable a break per trigger; stop or continue is the
// software's (4074)
void tta_console::apply_breaks()
{
	u8 en = 0;
	for (int k = 0; k < 4; k++) if (m_brk[k] != BRK_NONE) en |= u8(1 << k);
	port_w(2, u8((port(2) & 0xf0) | en));
}

// ---- consecutive events (CONS): port 71 bits 7-4 link trigger k to k-1 (bit 4 = 4->1,
// bit 5 = 1->2 ...), bits 3-2 what consecutive counts (00 cycles, 01 fetches, 10 emulator)
void tta_console::apply_cons()
{
	u8 v = u8(port(1) & 0x03);
	for (const std::vector<int> &s : m_cons)
		for (size_t j = 1; j < s.size(); j++) v |= u8(0x10 << s[j]);
	if (!m_cons.empty()) v |= u8(m_cons_mode == C_FET ? 0x04 : m_cons_mode == C_EMU ? 0x08 : 0x00);
	port_w(1, v);
}

// ---- acquisition (ACQ 2135-224F): all cycles (71 bit 1 and 72 bit 5) or trigger 4's; for n
// programs counter 5 to end it (timeout: output high for n counts, gated by trigger 4's edge
// with aftertrig4)
void tta_console::apply_acq()
{
	port_w(1, u8(m_acq_all ? port(1) | 0x02 : port(1) & ~0x02));
	port_w(2, u8(m_acq_all ? port(2) | 0x20 : port(2) & ~0x20));
	block &b = m_blk[4];
	if (m_acq_for) {
		static const u8 srcc[] = { 0xb, 0xb, 0xc, 0xd, 0xe, 0xf, 1, 2, 3, 4, 7, 8, 9, 0xa, 6, 5, 5, 5 };
		b.type = 2; b.mode = u16(((m_acq_after4 ? 0xc0 : 0x00) | srcc[m_acq_src]) << 8 | 0x02);
		b.load = b.hold = u16(m_acq_n);
	} else b = block();
	apply_counter(4, true);
}

void tta_console::clear_channel(int k)
{
	m_ev[k] = event_def(); m_ctr[k] = counter(); m_brk[k] = BRK_NONE;
	apply_event(k); apply_counter(k, true);
	apply_breaks();
}

void tta_console::clear_all()
{
	for (int k = 0; k < 4; k++) clear_channel(k);
	m_cons.clear(); m_cons_mode = C_CYC; apply_cons();
	m_acq_all = true; m_acq_for = m_acq_after4 = false; m_acq_n = 0; m_acq_src = S_NONE;
	apply_acq();
}

// ---- the resident module (TTA2) at the start and end of a run, and at INT 29
void tta_console::go(bool new_address)
{
	m_header = false;
	board().clear_break_status();
	if (new_address) {                             // 4108 = 7333: every counter disarmed, the programmed ones afresh
		port_w(7, 0xdf);
		for (int c = 0; c < 5; c++) if ((m_reprogram >> c) & 1) counter_sequence(c);
	}
	// 410A = 743F: the run marker (port 72 bit 4) becomes the opposite of the last sample's,
	// so that this run's acquisitions can be told apart
	const u8 last = ram(0x68, u8(port(5) - 1));
	port_w(2, u8((port(2) & ~0x10) | ((~last & 1) << 4)));
	// 4102 = 7325: trigger latches cleared, comparators on
	port_w(3, 0x00);
	port_w(0, u8(port(0) | 0x01));
}

void tta_console::stop()
{
	// 4106 = 7480: comparators off; the counters saved into their hold registers and read back
	port_w(0, u8(port(0) & ~0x01));
	port_w(7, 0xbf);
	for (int c = 0; c < 5; c++) {
		port_w(7, u8(0x11 + c));
		const u8 lo = port(6), hi = port(6);
		m_blk[c].hold = u16(lo | (hi << 8));
	}
	// the down-counters show what is left to count: 0 once done, else the count + 1 (they were
	// loaded with v - 1)
	const u8 st = port(7);
	for (int c = 0; c < 5; c++) {
		block &b = m_blk[c];
		if (b.type < 2) continue;
		const bool out = (st >> (c + 1)) & 1;
		bool done;
		if (b.type == 3) done = out;                 // delay: high when done
		else if (b.type == 2) done = !out;           // timeout: low when done
		else done = out || b.hold == 0xffff;         // pulse
		if (done) { b.hold = 0; continue; }
		if ((b.load >> 8) == 0 && (b.load & 0xff) < 2) b.hold = 1;
		else if (!((b.load >> 8) == 0 && (b.load & 0xff) == 2 && (b.mode & 0x80))) b.hold = u16(b.hold + 1);
	}
	m_filled = std::min(255, m_filled + run_samples());
}

// 4100 = 7300 and 4104 = 730C: the latched, enabled triggers; those not marked "continue"
// stop the program, the others print a trace line and the run goes on
bool tta_console::on_break(u8 trig, u16 pc)
{
	m_last_pc = pc;
	const u8 latched = u8(port(2) & port(3) & 0x0f) | trig;
	u8 stop = 0;
	for (int k = 0; k < 4; k++) if (((latched >> k) & 1) && m_brk[k] == BRK_STOP) stop |= u8(1 << k);
	port_w(2, port(2));                            // the request acknowledged (F2 bit 2)
	if (!stop) {
		trace_line(pc);
		port_w(3, 0x00);
		return false;
	}
	// DOS/50 V1.0 then prints the trace line and <BREAK ...>, naming TRIG1/TRIG2 only when its
	// own RTPA settings (BIF) ask for them, and never 3 or 4: the cause comes first
	std::string t;
	for (int k = 0; k < 4; k++) if ((stop >> k) & 1) t += fmt("%sTRIG%d", t.empty() ? "" : ", ", k + 1);
	sys.tta_print("\r\nTTA break: " + t + "\r\n");
	return true;
}

void tta_console::trace_line(u16 pc)
{
	tek8301 &m = sys.mdu_unit();
	const emulator_specifics *e = m.emulator();
	if (!e) return;
	std::string out;
	if (!m_header) { out += e->trace_header() + "\r\n"; m_header = true; }
	out += e->trace_line(pc, [&m](u16 a) { return m.progmem(a); }) + "\r\n";
	sys.tta_print(out);
}

// DISP 20F6/22D6: the current run's samples are those back from the pointer that carry its
// run marker (page 68 bit 0 = port 72 bit 4)
int tta_console::run_samples() const
{
	const u8 ptr = port(5), mark = u8((port(2) >> 4) & 1);
	int n = 0;
	while (n < 255 && (ram(0x68, u8(ptr - 1 - n)) & 1) == mark) n++;
	return n;
}

namespace {
struct line_parser {
	std::vector<std::string> t; size_t i = 0;
	bool s = false, c = false, n = false;
	explicit line_parser(const std::string &l) { std::stringstream ss(l); std::string w; while (ss >> w) t.push_back(w); }
	// pull -s/-c/-n out wherever they are (§2: "may be placed anywhere within a command line")
	void flags() {
		std::vector<std::string> r;
		for (auto &w : t) {
			std::string x = lower(w);
			if (x == "-s") { s = true; c = false; } else if (x == "-c") { c = true; s = false; } else if (x == "-n") n = true; else r.push_back(w);
		}
		t = r;
	}
	bool more() const { return i < t.size(); }
	std::string next() { if (i >= t.size()) throw error{ "missing parameter" }; return t[i++]; }
	std::string peek() const { return i < t.size() ? lower(t[i]) : ""; }
};
}

bool tta_console::is_command(const std::string &w0)
{
	static const char *n[] = { "acq", "ad", "bre", "bus", "cons", "cou", "ctr", "data", "disp", "eve", "pro", "qua", "tclr", "ts" };
	std::string w = lower(w0);
	for (const char *c : n) if (w == c) return true;
	return false;
}

std::string tta_console::run(const std::string &line)
{
	try { return command(line); }
	catch (const error &e) { return "Error: " + e.what + "\r\n"; }
}

std::string tta_console::command(const std::string &line)
{
	line_parser p(line);
	if (p.t.empty()) return "";
	const std::string cmd = lower(p.t[0]); p.i = 1;
	p.flags();
	u8 touched = 0;                                // events to rewrite
	bool breaks = false;
	// "the TTA stop or -s functions can be set only for the last event in a linked sequence"
	// (users manual, cons): the earlier events only qualify it. The board latches their
	// triggers all the same (TTA.TST module 31), so the rule is the software's
	auto qualifying_only = [this]() {
		u8 m = 0;
		for (const std::vector<int> &s : m_cons) for (size_t j = 0; j + 1 < s.size(); j++) m |= u8(1 << s[j]);
		return m;
	};
	const std::string last_only = "in a linked sequence (cons) only the last event can break";
	auto set_brk = [&](int k) {
		if ((p.s || p.c) && ((qualifying_only() >> k) & 1)) throw error{ last_only };
		if (p.s) m_brk[k] = BRK_STOP; else if (p.c) m_brk[k] = BRK_CONT;
		breaks = true;
	};
	auto finish = [&]() {
		for (int k = 0; k < 4; k++) if ((touched >> k) & 1) apply_event(k);
		if (breaks) apply_breaks();
		return std::string();
	};
	// "cmd all clr" | "cmd [-s|-c] N clr"
	auto target = [&](bool &all) -> int {
		std::string w = lower(p.next());
		if (is_all(w)) { all = true; return -1; }
		int k = channel(w); if (k < 0) throw error{ "trigger channel 1-4 expected" };
		all = false; return k;
	};
	auto each = [&](bool all, int k, const std::function<void(int)> &f) { if (all) { for (int j = 0; j < 4; j++) f(j); } else f(k); };

	auto set_field = [&](field &fd, const std::string &a, const std::string &b2, int bits) {
		number lo = parse_number(a, 16, bits);
		fd = field(); fd.on = true; fd.negate = p.n;
		if (lo.masked) {
			if (fd.negate) throw error{ "a range excluded (-n, an=, dn=) cannot have X digits" };   // EVE error 3F
			fd.masked = true; fd.lo = lo.v; fd.care = lo.care; return;
		}
		fd.lo = fd.hi = lo.v;
		if (!b2.empty()) {
			number hi = parse_number(b2, 16, bits);
			if (hi.masked) throw error{ "a range bound cannot have X digits" };   // EVE error 2C
			fd.hi = hi.v; if (fd.hi < fd.lo) throw error{ "upper value below lower value" };
		}
		// a range excluding the whole space is no condition (EVE: an= 0 to the largest address)
		if (fd.negate && fd.lo == 0 && fd.hi == ((1u << bits) - 1)) fd = field();
	};
	auto probe_value = [&](const std::string &v, u8 &val, u8 &care) {
		number nb = parse_number(v, 2, 8); val = u8(nb.v); care = u8(nb.care);
	};
	auto ctr_pattern = [&](const std::string &v, u8 &val, u8 &care) {
		if (v.size() != 4) throw error{ "counter pattern is four digits (1234): 1, 0 or X" };
		val = care = 0;
		for (int k = 0; k < 4; k++) {
			char ch = char(toupper(u8(v[size_t(k)])));
			if (ch == '1') { val |= u8(1 << k); care |= u8(1 << k); } else if (ch == '0') care |= u8(1 << k);
			else if (ch != 'X') throw error{ "counter pattern is four digits (1234): 1, 0 or X" };
		}
	};

	if (cmd == "ad" || cmd == "data") {
		bool all; int k = target(all);
		const bool ad = cmd == "ad";
		std::string a = p.next();
		if (lower(a) == "clr") { each(all, k, [&](int j) { (ad ? m_ev[j].addr : m_ev[j].data) = field(); touched |= u8(1 << j); }); if (!all) set_brk(k); return finish(); }
		if (all) throw error{ "all is for clr only" };
		std::string b2 = p.more() ? p.next() : "";
		set_field(ad ? m_ev[k].addr : m_ev[k].data, a, b2, ad ? 16 : 8);
		touched |= u8(1 << k);
		set_brk(k);
		return finish();
	}
	if (cmd == "bus" || cmd == "pro" || cmd == "ctr" || cmd == "qua") {
		bool all; int k = target(all);
		std::string a = p.next();
		if (lower(a) == "clr") {
			each(all, k, [&](int j) {
				event_def &e = m_ev[j];
				if (cmd == "bus") e.bus = 0; else if (cmd == "pro") e.probe_care = 0; else if (cmd == "ctr") e.ctr_care = 0; else e.qual = -1;
				touched |= u8(1 << j);
			});
			if (!all) set_brk(k);
			return finish();
		}
		if (all) throw error{ "all is for clr only" };
		event_def &e = m_ev[k];
		if (cmd == "bus") { u8 b = parse_bus(a); while (p.more()) b |= parse_bus(p.next()); e.bus = b; }
		else if (cmd == "pro") probe_value(a, e.probe_val, e.probe_care);
		else if (cmd == "ctr") ctr_pattern(a, e.ctr_val, e.ctr_care);
		else { if (a != "0" && a != "1") throw error{ "qualifier state is 0 or 1" }; e.qual = a[0] - '0'; }
		touched |= u8(1 << k);
		set_brk(k);
		return finish();
	}
	if (cmd == "eve") {
		bool all; int k = target(all);
		if (all) { if (lower(p.next()) != "clr") throw error{ "eve all clr" }; for (int j = 0; j < 4; j++) { m_ev[j] = event_def(); touched |= u8(1 << j); } return finish(); }
		event_def &e = m_ev[k];
		auto is_key = [](const std::string &w) { return w.find('=') != std::string::npos || lower(w) == "clr"; };
		while (p.more()) {
			std::string w = p.next(), lw = lower(w);
			if (lw == "clr") { e = event_def(); continue; }
			size_t eq = w.find('=');
			if (eq == std::string::npos) throw error{ "eve parameter expected: a= an= d= dn= b= p= c= q=" };
			std::string key = lower(w.substr(0, eq)), val = w.substr(eq + 1);
			if (key == "a" || key == "an" || key == "d" || key == "dn") {
				std::string b2 = p.more() && !is_key(p.t[p.i]) ? p.next() : "";
				bool keep = p.n; p.n = key.size() == 2;
				set_field(key[0] == 'a' ? e.addr : e.data, val, b2, key[0] == 'a' ? 16 : 8);
				p.n = keep;
			} else if (key == "b") {
				u8 b = parse_bus(val);
				while (p.more() && !is_key(p.t[p.i])) b |= parse_bus(p.next());
				e.bus = b;
			} else if (key == "p") probe_value(val, e.probe_val, e.probe_care);
			else if (key == "c") ctr_pattern(val, e.ctr_val, e.ctr_care);
			else if (key == "q") { if (val != "0" && val != "1") throw error{ "q= is 0 or 1" }; e.qual = val[0] - '0'; }
			else throw error{ "unknown eve parameter " + w };
		}
		touched |= u8(1 << k);
		set_brk(k);
		return finish();
	}
	if (cmd == "bre") {
		if (!p.more()) return show_bre();
		while (p.more()) {
			bool all; int k = target(all);
			brk_t b = BRK_STOP;
			std::string w = p.peek();
			if (w == "stop" || w == "s") { p.i++; b = BRK_STOP; }
			else if (w == "cont" || w == "c") { p.i++; b = BRK_CONT; }
			else if (w == "clr") { p.i++; b = BRK_NONE; }
			else if (p.c) b = BRK_CONT;
			each(all, k, [&](int j) {
				if (b != BRK_NONE && ((qualifying_only() >> j) & 1)) { if (all) return; throw error{ last_only }; }
				m_brk[j] = b;
			});
		}
		apply_breaks();
		return "";
	}
	if (cmd == "cons") {
		if (!p.more()) return "Consec " + cons_text() + "\r\n";
		std::string w = lower(p.next());
		if (w == "clr") { m_cons.clear(); apply_cons(); return ""; }
		cons_t mode;
		if (w == "cyc" || w == "c") mode = C_CYC; else if (w == "fet" || w == "f") mode = C_FET; else if (w == "emu" || w == "e") mode = C_EMU;
		else throw error{ "cons cyc|fet|emu sequence..." };
		std::vector<std::vector<int>> seqs; u8 used = 0;
		while (p.more()) {
			std::string q = p.next();
			if (q.size() < 2 || q.size() > 4) throw error{ "bad sequence " + q };
			std::vector<int> s;
			for (size_t j = 0; j < q.size(); j++) {
				int k = channel(std::string(1, q[j]));
				if (k < 0 || (j && k != (s.back() + 1) % 4)) throw error{ "sequences link consecutive triggers: 12 23 34 41 123 ... 4123" };
				if (used & (1 << k)) throw error{ "trigger " + std::to_string(k + 1) + " is in two sequences" };
				used |= u8(1 << k); s.push_back(k);
			}
			seqs.push_back(s);
		}
		if (seqs.empty()) throw error{ "missing sequence" };
		for (const std::vector<int> &s : seqs)
			for (size_t j = 0; j + 1 < s.size(); j++)
				if (m_brk[s[j]] != BRK_NONE) throw error{ last_only + ": clear trigger " + std::to_string(s[j] + 1) + "'s break first" };
		m_cons = seqs; m_cons_mode = mode;
		apply_cons();
		return "";
	}
	if (cmd == "cou") {
		bool all; int k = target(all);
		if (all) { if (lower(p.next()) != "clr") throw error{ "cou all clr" }; for (int j = 0; j < 4; j++) { m_ctr[j] = counter(); apply_counter(j, true); } return ""; }
		counter c = m_ctr[k];
		while (p.more()) {
			std::string w = p.next(), lw = lower(w);
			if (lw == "clr") { c = counter(); continue; }
			size_t eq = lw.find('=');
			if (eq == std::string::npos) throw error{ "cou parameter expected: v= s= g= o= r=" };
			std::string key = lw.substr(0, eq), val = lw.substr(eq + 1);
			if (key == "v") { number nb = parse_number(val, 10, 16); c.init = nb.v; c.has_value = true; }
			else if (key == "s") c.src = parse_src(val);
			else if (key == "o") {
				static const char *n[] = { "arm", "disarm", "pulse", "delay", "timeout" };
				int i = 0; while (i < 5 && val != n[i]) i++;
				if (i == 5) throw error{ "o=arm|disarm|pulse|delay|timeout" };
				c.mode = out_t(O_ARM + i);
			}
			else if (key == "g") {
				if (k == 0) throw error{ "the gate parameter is not allowed on counter 1" };
				static const char *n[] = { "off", "ctr", "trigh", "trigl", "seqh", "seql", "self" };
				int i = 0; while (i < 7 && val != n[i]) i++;
				if (i == 7) throw error{ "g=off|ctr|trigh|trigl|seqh|seql|self" };
				c.gate = gate_t(G_OFF + i);
			}
			else if (key == "r") { if (val != "on" && val != "off") throw error{ "r=on|off" }; c.restart = val == "on"; }
			else throw error{ "unknown cou parameter " + w };
		}
		if (c.restart == 1 && (c.gate == G_NONE || c.gate == G_OFF)) throw error{ "restart needs a gate (g=)" };
		const bool any = c.mode != O_NONE || c.has_value || c.src != S_NONE || c.gate != G_NONE || c.restart >= 0;
		if (any) {
			if (c.mode == O_NONE) c.mode = O_ARM;      // a counter given only a source just counts
			if (c.down() && c.has_value && c.init == 0) throw error{ "v must be 1-65535 for pulse, delay and timeout" };
			c.prog = true;
		}
		m_ctr[k] = c;
		apply_counter(k, true);
		set_brk(k);
		return finish();
	}
	if (cmd == "acq") {
		if (!p.more()) return acq_text(true);
		std::string w = lower(p.next());
		bool all;
		if (is_all(w)) all = true; else if (w == "ev4") all = false; else throw error{ "acq all|ev4 [for n source [aftertrig4]]" };
		bool f = false, a4 = false; u32 n = 0; src_t src = S_NONE;
		if (p.more()) {
			std::string fw = lower(p.next());
			if (fw != "for" && fw != "f") throw error{ "acq all|ev4 [for n source [aftertrig4]]" };
			number nb = parse_number(p.next(), 10, 16);
			if (nb.v < 1) throw error{ "the count is 1-65535" };
			n = nb.v; src = parse_src(p.next()); f = true;
			if (p.more()) {
				std::string a = lower(p.next());
				if (a != "aftertrig4" && a != "a") throw error{ "aftertrig4 expected" };
				if (nb.v < 2) throw error{ "with aftertrig4 the count is 2-65535" };
				a4 = true;
			}
		}
		m_acq_all = all; m_acq_for = f; m_acq_after4 = a4; m_acq_n = n; m_acq_src = src;
		apply_acq();
		return "";
	}
	if (cmd == "disp") {
		int n = -1; bool since = true;
		if (p.more()) {
			std::string w = lower(p.next());
			if (is_all(w) || w == "-a") { since = false; n = 255; }
			else { number nb = parse_number(w, 10, 16); if (nb.v < 1 || nb.v > 255) throw error{ "disp 1-255" }; n = int(nb.v); since = false; }
		}
		return display(n, since);
	}
	if (cmd == "tclr") {
		if (!p.more()) throw error{ "tclr -x | tclr 1-4... | tclr all" };
		if (lower(p.t[p.i]) == "-x") { clear_all(); return ""; }
		while (p.more()) { bool all; int k = target(all); each(all, k, [&](int j) { clear_channel(j); }); }
		return "";
	}
	if (cmd == "ts") {
		bool e = false, cc = false; u8 which = 0;
		for (size_t j = 1; j < p.t.size(); j++) {
			std::string w = lower(p.t[j]);
			if (w == "-e") e = true; else if (w == "-c") cc = true;
			else { int k = channel(w); if (k < 0) throw error{ "ts [-e] [-c] [1-4...]" }; which |= u8(1 << k); }
		}
		if (p.c) cc = true;     // flags() took -c as "continue"
		return status(which ? which : 0x0f, e || !cc, cc || !e);
	}
	throw error{ "unknown TTA command " + cmd };
}

std::string tta_console::cons_text() const
{
	if (m_cons.empty()) return "CLR";
	std::string s = m_cons_mode == C_CYC ? "CYC" : m_cons_mode == C_FET ? "FET" : "EMU";
	for (auto &q : m_cons) { s += ' '; for (int k : q) s += char('1' + k); }
	return s;
}

std::string tta_console::acq_text(bool with_remaining) const
{
	std::string s = std::string("Acquire ") + (m_acq_all ? "All" : "EV4");
	if (m_acq_for) s += fmt(" for %05uT %s%s", m_acq_n, src_name(m_acq_src), m_acq_after4 ? " AFTERTRIG4" : "");
	s += "\r\n";
	if (with_remaining && m_acq_for) s += fmt("Counts remaining: %05uT\r\n", unsigned(m_blk[4].hold));
	return s;
}

std::string tta_console::show_bre() const
{
	std::string s = fmt("%-16s", "");
	for (int k = 0; k < 4; k++) s += fmt("TRIGGER %d   ", k + 1);
	s += "\r\n" + fmt("%-16s", "Breakpoint");
	for (int k = 0; k < 4; k++) s += fmt(" %-11s", m_brk[k] == BRK_STOP ? "STOP" : m_brk[k] == BRK_CONT ? "CONT" : "---");
	return s + "\r\n";
}

// ts: the status display (Display 2-1)
std::string tta_console::status(u8 which, bool events, bool counters) const
{
	std::string o = fmt("%-16s", "");
	for (int k = 0; k < 4; k++) if (which & (1 << k)) o += fmt("TRIGGER %d   ", k + 1);
	o += "\r\n\r\n";
	auto row = [&](const char *label, const std::function<std::string(int)> &f) {
		std::string s = fmt("  %-14s", label);
		for (int k = 0; k < 4; k++) if (which & (1 << k)) s += fmt(" %-11s", f(k).c_str());
		o += s + "\r\n";
	};
	const bool single = (which & (which - 1)) == 0;
	auto lower_v = [&](const field &f, int digits) -> std::string {
		if (!f.on) return "---";
		if (f.masked && single) {      // ts n expands the don't-care digits
			std::string s;
			for (int d = digits - 1; d >= 0; d--) { u32 m = 0xfu << (4 * d); s += (f.care & m) == m ? fmt("%X", (f.lo >> (4 * d)) & 0xf) : "X"; }
			return s;
		}
		return fmt("%0*X", digits, f.masked ? (f.lo & f.care) : f.lo);
	};
	auto upper_v = [&](const field &f, int digits) -> std::string {
		if (!f.on) return "---";
		if (f.masked) return single ? "" : fmt("%0*X M", digits, f.care);
		return fmt("%0*X %s", digits, f.hi, f.negate ? "N" : "R");
	};
	if (events) {
		o += "EVENT\r\n";
		row("Lower Address", [&](int k) { return lower_v(m_ev[k].addr, 4); });
		row("Upper Address", [&](int k) { return upper_v(m_ev[k].addr, 4); });
		row("Lower Data", [&](int k) { return lower_v(m_ev[k].data, 2); });
		row("Upper Data", [&](int k) { return upper_v(m_ev[k].data, 2); });
		row("Bus", [&](int k) { return m_ev[k].bus ? bus_text(m_ev[k].bus) : std::string("---"); });
		row("Probe (7-0)", [&](int k) {
			const event_def &e = m_ev[k]; if (!e.probe_care) return std::string("---");
			std::string s; for (int b = 7; b >= 0; b--) s += !((e.probe_care >> b) & 1) ? 'X' : ((e.probe_val >> b) & 1) ? '1' : '0';
			return s; });
		row("Qualifier (X)", [&](int k) { return m_ev[k].qual < 0 ? std::string("---") : std::to_string(m_ev[k].qual); });
		row("Ctr (1234)", [&](int k) {
			const event_def &e = m_ev[k]; if (!e.ctr_care) return std::string("---");
			std::string s; for (int b = 0; b < 4; b++) s += !((e.ctr_care >> b) & 1) ? 'X' : ((e.ctr_val >> b) & 1) ? '1' : '0';
			return s; });
		o += "  Consec " + cons_text() + "\r\n";
	}
	if (counters) {
		const u8 outs = board().outputs();
		o += "COUNTER\r\n";
		row("Current Output", [&](int k) { return std::string(" ") + (((outs >> k) & 1) ? "1" : "0"); });
		row("Output Mode", [&](int k) { return std::string(m_ctr[k].prog ? out_name(m_ctr[k].mode) : "---"); });
		row("Initial Value", [&](int k) { return m_ctr[k].prog && m_ctr[k].has_value ? fmt("%05uT", m_ctr[k].init) : std::string("---"); });
		row("Current Value", [&](int k) { return m_ctr[k].prog ? fmt("%05uT", unsigned(m_blk[k].hold)) : std::string("---"); });
		row("Source", [&](int k) { return std::string(m_ctr[k].prog ? src_name(m_ctr[k].src) : "---"); });
		row("Gate", [&](int k) { return std::string(m_ctr[k].prog ? gate_name(m_ctr[k].gate) : "---"); });
		row("Restart", [&](int k) { return m_ctr[k].prog && m_ctr[k].restart >= 0 ? std::string(m_ctr[k].restart ? "ON" : "OFF") : std::string("---"); });
	}
	if (events && counters) {
		std::string s = fmt("%-16s", "Breakpoint");
		for (int k = 0; k < 4; k++) if (which & (1 << k)) s += fmt(" %-11s", m_brk[k] == BRK_STOP ? "STOP" : m_brk[k] == BRK_CONT ? "CONT" : "---");
		o += s + "\r\n" + acq_text(false);
	}
	return o;
}

// disp: acquisition memory, oldest first (Display 1-2), read from the board: the current run
// (since_start), the last n, or all; the header repeats every 30 lines, and a dotted line marks
// each start of the emulator (where the run marker changes)
std::string tta_console::display(int n, bool since_start) const
{
	const char *hdr = " ADDR   DATA  MNEMONIC        7-PROBE-0    BUS\r\n";
	const emulator_specifics *e = sys.mdu_unit().emulator();
	const int count = since_start ? run_samples() : std::min(n, m_filled);
	const u8 ptr = port(5);
	std::string o = hdr;
	int lines = 0;
	for (int i = count; i > 0; i--) {
		const u8 r = u8(ptr - i);
		const u8 act = ram(0x68, r), bus = ram(0x69, r), pr = ram(0x6a, r), data = ram(0x6b, r);
		const u32 addr = u32(ram(0x6d, r) | (ram(0x6e, r) << 8) | (ram(0x6f, r) << 16));
		if (i < count && (act & 1) != (ram(0x68, u8(r - 1)) & 1)) o += "  . . . . . . . . . . . . . . . . . . . . .\r\n";
		if (lines == 30) { o += std::string("\r\n") + hdr; lines = 0; }
		std::string ps;
		for (int b = 7; b >= 0; b--) { ps += ((pr >> b) & 1) ? '1' : '0'; if (b == 4) ps += ' '; }
		// the 8085 support's decode of the bus status byte: M/I, RD/WT, F
		std::string bs = std::string((bus & 4) ? "M" : "I") + ((bus & 2) ? " RD" : " WT") + ((bus & 1) ? " F" : "");
		o += fmt("%06X  %02X   %-16s%s   %s\r\n", addr, data, (bus & 1) && e ? e->opcode(data).c_str() : "", ps.c_str(), bs.c_str());
		lines++;
	}
	return o;
}
