// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA command set -- see tta_console.h. Syntax, parameters and displays follow the
// Trigger Trace Analyzer users manual 070-3760-01, section 2 (and the 8540 OS/40 manual's
// summary of the same commands where the TTA manual is silent).
#include "system/tta_console.h"
#include "system/tek8550.h"
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace {
using R = tta_run;

std::string lower(std::string s) { for (char &c : s) c = char(tolower(u8(c))); return s; }
std::string upper(std::string s) { for (char &c : s) c = char(toupper(u8(c))); return s; }
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

const char *src_name(R::src_t s)
{
	static const char *n[] = { "---", "200NSEC", "2USEC", "20USEC", "200USEC", "2MSEC", "EV1", "EV2", "EV3", "EV4",
		"TRIG1", "TRIG2", "TRIG3", "TRIG4", "ACQ", "CYC", "EMUCLK", "QUA" };
	return n[s];
}
R::src_t parse_src(const std::string &t0)
{
	std::string t = lower(t0);
	if (t.compare(0, 2, "s=") == 0) t = t.substr(2);
	for (int i = R::S_200NS; i <= R::S_QUA; i++) if (lower(src_name(R::src_t(i))) == t) return R::src_t(i);
	if (t == "evl") return R::S_EV1;                // as printed in the manual's examples
	throw error{ "bad source " + t0 };
}
const char *out_name(R::out_t o) { static const char *n[] = { "---", "ARM", "DISARM", "PULSE", "DELAY", "TIMEOUT" }; return n[o]; }
const char *gate_name(R::gate_t g) { static const char *n[] = { "---", "OFF", "CTR", "TRIGH", "TRIGL", "SEQH", "SEQL", "SELF" }; return n[g]; }

// bus symbols (the 8085A set the manual uses; the Z80 board takes the same ones)
u8 parse_bus(const std::string &t)
{
	std::string s = lower(t);
	if (s == "f") return R::B_F;
	if (s == "nf") return R::B_NF;
	if (s == "m") return R::B_M;
	if (s == "i") return R::B_I;
	if (s == "rd") return R::B_RD;
	if (s == "wt") return R::B_WT;
	throw error{ "bad bus symbol " + t };
}
std::string bus_text(u8 b)
{
	std::string s;
	auto add = [&](u8 m, const char *n) { if (b & m) { if (!s.empty()) s += ' '; s += n; } };
	add(R::B_M, "M"); add(R::B_I, "I"); add(R::B_RD, "RD"); add(R::B_WT, "WT"); add(R::B_F, "F"); add(R::B_NF, "NF");
	return s;
}

}

tta_console::tta_console(tek8550_system &s) : sys(s)
{
	tta &b = sys.mdu_unit().tta_board;
	b.run.clear_all();
	b.port_write(0, 0x05);                 // run mode: comparators on, clock running
	// cont: a trace line per trigger, as the TTA software printed (§1-11). stop: DOS/50 prints
	// the trace line and <BREAK ...>; where its support for the processor cannot name a TTA
	// trigger (or the trigger is 3 or 4, which no DOS/50 status shows), the cause comes first
	b.run.on_break = [this](u8 trig, u16, bool stop) {
		if (!stop) { trace_line(); return; }
		const emulator_specifics *e = sys.mdu_unit().emulator();
		if (e && e->names_tta_breaks() && !(trig & 0x0c)) return;
		std::string t;
		for (int k = 0; k < 4; k++) if ((trig >> k) & 1) t += fmt("%sTRIG%d", t.empty() ? "" : ", ", k + 1);
		sys.tta_print("\r\nTTA break: " + t + "\r\n");
	};
	b.run.on_start = [this]() { m_header = false; };
}

void tta_console::trace_line()
{
	tek8301 &m = sys.mdu_unit();
	const emulator_specifics *e = m.emulator();
	if (!e) return;
	std::string out;
	if (!m_header) { out += e->trace_header() + "\r\n"; m_header = true; }
	out += e->trace_line(m.tta_board.run.last_pc(), [&m](u16 a) { return m.progmem(a); }) + "\r\n";
	sys.tta_print(out);
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
	tta_run &r = sys.mdu_unit().tta_board.run;
	line_parser p(line);
	if (p.t.empty()) return "";
	const std::string cmd = lower(p.t[0]); p.i = 1;
	p.flags();
	auto set_brk = [&](int k) { if (p.s) r.brk[k] = R::BRK_STOP; else if (p.c) r.brk[k] = R::BRK_CONT; };
	// "cmd all clr" | "cmd [-s|-c] N clr"
	auto target = [&](bool &all) -> int {
		std::string w = lower(p.next());
		if (is_all(w)) { all = true; return -1; }
		int k = channel(w); if (k < 0) throw error{ "trigger channel 1-4 expected" };
		all = false; return k;
	};
	auto each = [&](bool all, int k, const std::function<void(int)> &f) { if (all) { for (int j = 0; j < 4; j++) f(j); } else f(k); };

	auto set_field = [&](R::field &fd, const std::string &a, const std::string &b2, int bits) {
		number lo = parse_number(a, 16, bits);
		fd = R::field(); fd.on = true; fd.negate = p.n;
		if (lo.masked) { fd.masked = true; fd.lo = lo.v; fd.care = lo.care; return; }
		fd.lo = fd.hi = lo.v;
		if (!b2.empty()) { number hi = parse_number(b2, 16, bits); fd.hi = hi.v; if (fd.hi < fd.lo) throw error{ "upper value below lower value" }; }
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
		if (lower(a) == "clr") { each(all, k, [&](int j) { (ad ? r.ev[j].addr : r.ev[j].data) = R::field(); }); if (!all) set_brk(k); return ""; }
		if (all) throw error{ "all is for clr only" };
		std::string b2 = p.more() ? p.next() : "";
		set_field(ad ? r.ev[k].addr : r.ev[k].data, a, b2, ad ? 16 : 8);
		set_brk(k);
		return "";
	}
	if (cmd == "bus" || cmd == "pro" || cmd == "ctr" || cmd == "qua") {
		bool all; int k = target(all);
		std::string a = p.next();
		if (lower(a) == "clr") {
			each(all, k, [&](int j) {
				R::event_def &e = r.ev[j];
				if (cmd == "bus") e.bus = 0; else if (cmd == "pro") e.probe_care = 0; else if (cmd == "ctr") e.ctr_care = 0; else e.qual = -1;
			});
			if (!all) set_brk(k);
			return "";
		}
		if (all) throw error{ "all is for clr only" };
		R::event_def &e = r.ev[k];
		if (cmd == "bus") { u8 b = parse_bus(a); while (p.more()) b |= parse_bus(p.next()); e.bus = b; }
		else if (cmd == "pro") probe_value(a, e.probe_val, e.probe_care);
		else if (cmd == "ctr") ctr_pattern(a, e.ctr_val, e.ctr_care);
		else { if (a != "0" && a != "1") throw error{ "qualifier state is 0 or 1" }; e.qual = a[0] - '0'; }
		set_brk(k);
		return "";
	}
	if (cmd == "eve") {
		bool all; int k = target(all);
		if (all) { if (lower(p.next()) != "clr") throw error{ "eve all clr" }; for (auto &e : r.ev) e = R::event_def(); return ""; }
		R::event_def &e = r.ev[k];
		auto is_key = [](const std::string &w) { return w.find('=') != std::string::npos || lower(w) == "clr"; };
		while (p.more()) {
			std::string w = p.next(), lw = lower(w);
			if (lw == "clr") { e = R::event_def(); continue; }
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
		set_brk(k);
		return "";
	}
	if (cmd == "bre") {
		if (!p.more()) return show_bre();
		while (p.more()) {
			bool all; int k = target(all);
			R::brk_t b = R::BRK_STOP;
			std::string w = p.peek();
			if (w == "stop" || w == "s") { p.i++; b = R::BRK_STOP; }
			else if (w == "cont" || w == "c") { p.i++; b = R::BRK_CONT; }
			else if (w == "clr") { p.i++; b = R::BRK_NONE; }
			else if (p.c) b = R::BRK_CONT;
			each(all, k, [&](int j) { r.brk[j] = b; });
		}
		return "";
	}
	if (cmd == "cons") {
		if (!p.more()) return "Consec " + cons_text() + "\r\n";
		std::string w = lower(p.next());
		if (w == "clr") { r.cons.clear(); return ""; }
		R::cons_t mode;
		if (w == "cyc" || w == "c") mode = R::C_CYC; else if (w == "fet" || w == "f") mode = R::C_FET; else if (w == "emu" || w == "e") mode = R::C_EMU;
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
		r.cons = seqs; r.cons_mode = mode;
		return "";
	}
	if (cmd == "cou") {
		bool all; int k = target(all);
		if (all) { if (lower(p.next()) != "clr") throw error{ "cou all clr" }; for (auto &c : r.ctr) c = R::counter(); return ""; }
		R::counter &c = r.ctr[k];
		while (p.more()) {
			std::string w = p.next(), lw = lower(w);
			if (lw == "clr") { c = R::counter(); continue; }
			size_t eq = lw.find('=');
			if (eq == std::string::npos) throw error{ "cou parameter expected: v= s= g= o= r=" };
			std::string key = lw.substr(0, eq), val = lw.substr(eq + 1);
			if (key == "v") { number nb = parse_number(val, 10, 16); c.init = nb.v; c.has_value = true; }
			else if (key == "s") c.src = parse_src(val);
			else if (key == "o") {
				static const char *n[] = { "arm", "disarm", "pulse", "delay", "timeout" };
				int i = 0; while (i < 5 && val != n[i]) i++;
				if (i == 5) throw error{ "o=arm|disarm|pulse|delay|timeout" };
				c.mode = R::out_t(R::O_ARM + i);
			}
			else if (key == "g") {
				if (k == 0) throw error{ "the gate parameter is not allowed on counter 1" };
				static const char *n[] = { "off", "ctr", "trigh", "trigl", "seqh", "seql", "self" };
				int i = 0; while (i < 7 && val != n[i]) i++;
				if (i == 7) throw error{ "g=off|ctr|trigh|trigl|seqh|seql|self" };
				c.gate = R::gate_t(R::G_OFF + i);
			}
			else if (key == "r") { if (val != "on" && val != "off") throw error{ "r=on|off" }; c.restart = val == "on"; }
			else throw error{ "unknown cou parameter " + w };
		}
		if (c.restart == 1 && (c.gate == R::G_NONE || c.gate == R::G_OFF)) throw error{ "restart needs a gate (g=)" };
		const bool any = c.mode != R::O_NONE || c.has_value || c.src != R::S_NONE || c.gate != R::G_NONE || c.restart >= 0;
		if (any) {
			if (c.mode == R::O_NONE) c.mode = R::O_ARM;      // a counter given only a source just counts
			if (c.down() && c.has_value && c.init == 0) throw error{ "v must be 1-65535 for pulse, delay and timeout" };
			c.prog = true; c.load();
		}
		set_brk(k);
		return "";
	}
	if (cmd == "acq") {
		if (!p.more()) return acq_text(true);
		std::string w = lower(p.next());
		if (is_all(w)) r.acq_mode = R::A_ALL; else if (w == "ev4") r.acq_mode = R::A_EV4; else throw error{ "acq all|ev4 [for n source [aftertrig4]]" };
		r.acq_for = r.acq_after4 = false;
		if (p.more()) {
			std::string f = lower(p.next());
			if (f != "for" && f != "f") throw error{ "acq all|ev4 [for n source [aftertrig4]]" };
			number nb = parse_number(p.next(), 10, 16);
			if (nb.v < 1) throw error{ "the count is 1-65535" };
			r.acq_n = nb.v; r.acq_src = parse_src(p.next()); r.acq_for = true;
			if (p.more()) {
				std::string a = lower(p.next());
				if (a != "aftertrig4" && a != "a") throw error{ "aftertrig4 expected" };
				if (nb.v < 2) throw error{ "with aftertrig4 the count is 2-65535" };
				r.acq_after4 = true;
			}
		}
		return "";
	}
	if (cmd == "disp") {
		int n = -1; bool since = true;
		if (p.more()) {
			std::string w = lower(p.next());
			if (is_all(w) || w == "-a") { since = false; n = 255; }
			else { number nb = parse_number(w, 10, 16); if (nb.v < 1 || nb.v > 255) throw error{ "disp 1-255" }; n = int(nb.v); since = false; }
		}
		return display(r.samples(n < 0 ? 255 : n, since));
	}
	if (cmd == "tclr") {
		if (!p.more()) throw error{ "tclr -x | tclr 1-4... | tclr all" };
		if (lower(p.t[p.i]) == "-x") { r.clear_all(); return ""; }
		while (p.more()) { bool all; int k = target(all); each(all, k, [&](int j) { r.clear_channel(j); }); }
		return "";
	}
	if (cmd == "ts") {
		bool e = false, cc = false; u8 which = 0;
		std::vector<std::string> rest;
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
	const tta_run &r = sys.mdu_unit().tta_board.run;
	if (r.cons.empty()) return "CLR";
	std::string s = r.cons_mode == R::C_CYC ? "CYC" : r.cons_mode == R::C_FET ? "FET" : "EMU";
	for (auto &q : r.cons) { s += ' '; for (int k : q) s += char('1' + k); }
	return s;
}

std::string tta_console::acq_text(bool with_remaining) const
{
	const tta_run &r = sys.mdu_unit().tta_board.run;
	std::string s = std::string("Acquire ") + (r.acq_mode == R::A_ALL ? "All" : "EV4");
	if (r.acq_for) s += fmt(" for %05uT %s%s", r.acq_n, src_name(r.acq_src), r.acq_after4 ? " AFTERTRIG4" : "");
	s += "\r\n";
	if (with_remaining && r.acq_for) s += fmt("Counts remaining: %05uT\r\n", r.acq_remaining());
	return s;
}

std::string tta_console::show_bre() const
{
	const tta_run &r = sys.mdu_unit().tta_board.run;
	std::string s = fmt("%-16s", "");
	for (int k = 0; k < 4; k++) s += fmt("TRIGGER %d   ", k + 1);
	s += "\r\n" + fmt("%-16s", "Breakpoint");
	for (int k = 0; k < 4; k++) s += fmt(" %-11s", r.brk[k] == R::BRK_STOP ? "STOP" : r.brk[k] == R::BRK_CONT ? "CONT" : "---");
	return s + "\r\n";
}

// ts: the status display (Display 2-1)
std::string tta_console::status(u8 which, bool events, bool counters) const
{
	const tta_run &r = sys.mdu_unit().tta_board.run;
	std::string o = fmt("%-16s", "");
	for (int k = 0; k < 4; k++) if (which & (1 << k)) o += fmt("TRIGGER %d   ", k + 1);
	o += "\r\n\r\n";
	auto row = [&](const char *label, const std::function<std::string(int)> &f) {
		std::string s = fmt("  %-14s", label);
		for (int k = 0; k < 4; k++) if (which & (1 << k)) s += fmt(" %-11s", f(k).c_str());
		o += s + "\r\n";
	};
	const bool single = (which & (which - 1)) == 0;
	auto lower_v = [&](const R::field &f, int digits) -> std::string {
		if (!f.on) return "---";
		if (f.masked && single) {      // ts n expands the don't-care digits
			std::string s;
			for (int d = digits - 1; d >= 0; d--) { u32 m = 0xfu << (4 * d); s += (f.care & m) == m ? fmt("%X", (f.lo >> (4 * d)) & 0xf) : "X"; }
			return s;
		}
		return fmt("%0*X", digits, f.masked ? (f.lo & f.care) : f.lo);
	};
	auto upper_v = [&](const R::field &f, int digits) -> std::string {
		if (!f.on) return "---";
		if (f.masked) return single ? "" : fmt("%0*X M", digits, f.care);
		return fmt("%0*X %s", digits, f.hi, f.negate ? "N" : "R");
	};
	if (events) {
		o += "EVENT\r\n";
		row("Lower Address", [&](int k) { return lower_v(r.ev[k].addr, 4); });
		row("Upper Address", [&](int k) { return upper_v(r.ev[k].addr, 4); });
		row("Lower Data", [&](int k) { return lower_v(r.ev[k].data, 2); });
		row("Upper Data", [&](int k) { return upper_v(r.ev[k].data, 2); });
		row("Bus", [&](int k) { return r.ev[k].bus ? bus_text(r.ev[k].bus) : std::string("---"); });
		row("Probe (7-0)", [&](int k) {
			const R::event_def &e = r.ev[k]; if (!e.probe_care) return std::string("---");
			std::string s; for (int b = 7; b >= 0; b--) s += !((e.probe_care >> b) & 1) ? 'X' : ((e.probe_val >> b) & 1) ? '1' : '0';
			return s; });
		row("Qualifier (X)", [&](int k) { return r.ev[k].qual < 0 ? std::string("---") : std::to_string(r.ev[k].qual); });
		row("Ctr (1234)", [&](int k) {
			const R::event_def &e = r.ev[k]; if (!e.ctr_care) return std::string("---");
			std::string s; for (int b = 0; b < 4; b++) s += !((e.ctr_care >> b) & 1) ? 'X' : ((e.ctr_val >> b) & 1) ? '1' : '0';
			return s; });
		o += "  Consec " + cons_text() + "\r\n";
	}
	if (counters) {
		o += "COUNTER\r\n";
		row("Current Output", [&](int k) { return std::string(" ") + (r.ctr[k].output() ? "1" : "0"); });
		row("Output Mode", [&](int k) { return std::string(r.ctr[k].prog ? out_name(r.ctr[k].mode) : "---"); });
		row("Initial Value", [&](int k) { return r.ctr[k].prog && r.ctr[k].has_value ? fmt("%05uT", r.ctr[k].init) : std::string("---"); });
		row("Current Value", [&](int k) { return r.ctr[k].prog ? fmt("%05uT", r.ctr[k].cur) : std::string("---"); });
		row("Source", [&](int k) { return std::string(r.ctr[k].prog ? src_name(r.ctr[k].src) : "---"); });
		row("Gate", [&](int k) { return std::string(r.ctr[k].prog ? gate_name(r.ctr[k].gate) : "---"); });
		row("Restart", [&](int k) { return r.ctr[k].prog && r.ctr[k].restart >= 0 ? std::string(r.ctr[k].restart ? "ON" : "OFF") : std::string("---"); });
	}
	if (events && counters) {
		std::string s = fmt("%-16s", "Breakpoint");
		for (int k = 0; k < 4; k++) if (which & (1 << k)) s += fmt(" %-11s", r.brk[k] == R::BRK_STOP ? "STOP" : r.brk[k] == R::BRK_CONT ? "CONT" : "---");
		o += s + "\r\n" + acq_text(false);
	}
	return o;
}

// disp: acquisition memory, oldest first (Display 1-2); the header repeats every 30 lines,
// and a dotted line marks each start of the emulator
std::string tta_console::display(const std::vector<tta_run::sample> &v) const
{
	const char *hdr = " ADDR   DATA  MNEMONIC        7-PROBE-0    BUS\r\n";
	const emulator_specifics *e = sys.mdu_unit().emulator();
	std::string o = hdr;
	int lines = 0;
	for (size_t i = 0; i < v.size(); i++) {
		const tta_run::sample &s = v[i];
		if (s.first && i > 0) o += "  . . . . . . . . . . . . . . . . . . . . .\r\n";
		if (lines == 30) { o += std::string("\r\n") + hdr; lines = 0; }
		std::string pr;
		for (int b = 7; b >= 0; b--) { pr += ((s.probe >> b) & 1) ? '1' : '0'; if (b == 4) pr += ' '; }
		o += fmt("%06X  %02X   %-16s%s   %s\r\n", s.addr, s.data, s.type == tta_run::FETCH && e ? e->opcode(s.data).c_str() : "", pr.c_str(), tta_run::bus_symbols(s.type).c_str());
		lines++;
	}
	return o;
}
