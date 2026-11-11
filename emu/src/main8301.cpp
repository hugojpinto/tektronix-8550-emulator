// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// tek8301: run the Tektronix 8301 MDU stand-alone; system terminal port on this terminal.
//   tek8301 [-roms DIR] [-sw HEX] [-cycles N] [-trace FILE] [-io] [-log FILE]
// Ctrl-] enters the monitor.
#include "machine/tek8301.h"
#include "cpu/s2650/2650dasm.h"
#include "host/tty.h"
#include <cstdio>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <memory>

struct cfg_t : s2650_disassembler::config { bool get_z80_mnemonics_mode() const override { return false; } } g_cfg;

static std::string dasm(tek8301 &m, u16 pc, int *len = nullptr)
{
	s2650_disassembler d(&g_cfg); data_buffer b;
	b.rd = [&](offs_t a) -> u8 { return m.read(a & 0x7fff); };
	std::ostringstream s; u32 r = d.disassemble(s, pc, b, b);
	if (len) *len = int(r & util::disasm_interface::LENGTHMASK);
	return s.str();
}

static void show_regs(tek8301 &m)
{
	auto &c = m.cpu;
	printf("R0=%02x R1=%02x R2=%02x R3=%02x R1'=%02x R2'=%02x R3'=%02x PSU=%02x PSL=%02x PC=%04x %s LED=%02x RAMLED=%02x cyc=%llu insn=%llu\r\n",
		c.reg(0), c.reg(1), c.reg(2), c.reg(3), c.reg(4), c.reg(5), c.reg(6), c.psu(), c.psl(), c.pc(),
		c.halted() ? "HALT" : "run", m.leds(), m.ram_leds(), (unsigned long long)m.cycles(), (unsigned long long)c.insn_count());
	printf("  %04x: %s\r\n", c.pc(), dasm(m, c.pc()).c_str());
}

static bool monitor(tek8301 &m)
{
	host::tty_raw(false);
	printf("\n[monitor] "); show_regs(m);
	char line[256];
	for (;;) {
		printf("mon> "); fflush(stdout);
		if (!fgets(line, sizeof line, stdin)) return false;
		unsigned a = 0, n = 0; int k = sscanf(line + 1, "%x %x", &a, &n);
		switch (line[0]) {
		case 'q': return false;
		case 'c': case '\n': host::tty_raw(true); return true;
		case 'r': show_regs(m); break;
		case 'd': { if (k < 1) a = m.cpu.pc(); if (k < 2) n = 64;
			for (unsigned i = 0; i < n; i++) { if (i % 16 == 0) printf("%s%04x:", i ? "\n" : "", (a + i) & 0x7fff); printf(" %02x", m.read((a + i) & 0x7fff)); }
			printf("\n"); break; }
		case 'u': { if (k < 1) a = m.cpu.pc(); if (k < 2) n = 16; u16 pc = u16(a);
			for (unsigned i = 0; i < n; i++) { int len; std::string s = dasm(m, pc, &len); printf("%04x: %s\n", pc, s.c_str()); pc += len ? len : 1; }
			break; }
		case 's': { if (k < 1) a = 1; for (unsigned i = 0; i < a; i++) m.cpu.run(1); show_regs(m); break; }
		case 'i': m.trace_io = !m.trace_io; printf("io trace %s\n", m.trace_io ? "on" : "off"); break;
		case 'R': m.restart(); show_regs(m); break;
		case 'h': printf("c continue | q quit | r regs | d [addr [n]] | u [addr [n]] | s [n] step | i io-trace | R restart\n"); break;
		default: printf("?\n");
		}
	}
}

int main(int argc, char **argv)
{
	std::string roms = "../roms/8550/8301", tracefile, logfile;
	long long max_cycles = -1; int sw = 0; bool pm_ext = false; bool io = false;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "-roms") roms = next();
		else if (a == "-sw") sw = int(strtol(next().c_str(), nullptr, 16));
		else if (a == "-pm") pm_ext = next() == "ext";
		else if (a == "-cycles") max_cycles = atoll(next().c_str());
		else if (a == "-trace") tracefile = next();
		else if (a == "-log") logfile = next();
		else if (a == "-io") io = true;
		else { fprintf(stderr, "usage: %s [-roms DIR] [-sw HEX] [-cycles N] [-trace FILE] [-io] [-log FILE]\n", argv[0]); return 1; }
	}
	auto m = std::make_unique<tek8301>();
	std::string err;
	if (!m->load_roms(roms, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	m->mode_switch = u8(sw);
	m->pm_ext_comparator = pm_ext; m->trace_io = io;
	FILE *logf = logfile.empty() ? nullptr : fopen(logfile.c_str(), "wb");
	m->term.on_tx = [&](u8 c) { c &= 0x7f; if (c) { fputc(c, stdout); fflush(stdout); } if (logf) { fputc(c, logf); fflush(logf); } };
	m->aux.on_tx = [&](u8 c) { if (logf) fprintf(logf, "[AUX %02x]", c); };
	m->remote.on_tx = [&](u8 c) { if (logf) fprintf(logf, "[REM %02x]", c); };
	m->hsi_tx = [&](u8 c) { if (logf) fprintf(logf, "[HSI %02x]", c); };
	FILE *tf = tracefile.empty() ? nullptr : fopen(tracefile.c_str(), "w");
	if (tf) m->cpu.trace_hook = [&](u16 pc) { fprintf(tf, "%04x: %s\n", pc, dasm(*m, pc).c_str()); };

	m->power_on();
	bool interactive = host::is_tty(0) && max_cycles < 0;
	if (interactive) { host::tty_raw(true); fprintf(stderr, "[tek8301: Ctrl-] for monitor]\r\n"); }
	using clk = std::chrono::steady_clock;
	auto t0 = clk::now();
	const int slice = int(tek8301::CPU_HZ / 100);
	for (;;) {
		if (max_cycles >= 0 && (long long)m->cycles() >= max_cycles) break;
		int k;
		while ((k = host::poll_key()) >= 0) {
			if (k == 035) { if (!monitor(*m)) goto done; continue; }
			m->term.host_rx(u8(k == '\n' ? '\r' : k));
		}
		m->run_slice(slice);
		if (interactive) {
			auto want = t0 + std::chrono::microseconds(long(m->cycles() / tek8301::CPU_HZ * 1e6));
			if (want > clk::now()) std::this_thread::sleep_until(want);
		}
	}
done:
	host::tty_raw(false);
	if (!interactive) { fprintf(stderr, "\n"); show_regs(*m); }
	if (tf) fclose(tf);
	if (logf) fclose(logf);
	return 0;
}
