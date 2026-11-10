// tek8501: run the Tektronix 8501 DMU stand-alone, AUX port on this terminal.
//
//   tek8501 [-roms DIR] [-0 disk.imd] [-1 disk.imd] [-trace FILE] [-cycles N] [-log FILE]
//
// Ctrl-] enters the emulator monitor (type 'h' there for help).
#include "machine/tek8501.h"
#include "cpu/t11/t11dasm.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include "host/tty.h"

using host::tty_raw;
using host::poll_key;

static std::string dasm(tek8501 &m, u16 pc, int *len = nullptr)
{
	t11_disassembler d; data_buffer b;
	b.rd = [&](offs_t a) -> u8 { u16 w = m.peek16(a & 0xfffe); return (a & 1) ? u8(w >> 8) : u8(w); };
	std::ostringstream s; u32 r = d.disassemble(s, pc, b, b);
	if (len) *len = int(r & util::disasm_interface::LENGTHMASK);
	return s.str();
}

static void show_regs(tek8501 &m)
{
	auto &c = m.cpu;
	printf("R0=%06o R1=%06o R2=%06o R3=%06o R4=%06o R5=%06o SP=%06o PC=%06o PSW=%03o %s  LEDs=%02o cyc=%llu insn=%llu\r\n",
		c.reg(0), c.reg(1), c.reg(2), c.reg(3), c.reg(4), c.reg(5), c.reg(6), c.reg(7), c.psw(),
		c.halted() ? "HALTED" : c.waiting() ? "WAIT" : "run", m.leds(),
		(unsigned long long)m.cycles(), (unsigned long long)c.insn_count());
	printf("  %06o: %s\r\n", c.reg(7), dasm(m, c.reg(7)).c_str());
}

// returns false to quit
static bool monitor(tek8501 &m)
{
	tty_raw(false);
	printf("\n[monitor] ");
	show_regs(m);
	char line[256];
	for (;;) {
		printf("mon> "); fflush(stdout);
		if (!fgets(line, sizeof line, stdin)) return false;
		char cmd = line[0]; unsigned a = 0, n = 0;
		int k = sscanf(line + 1, "%o %o", &a, &n);
		switch (cmd) {
		case 'q': return false;
		case 'c': case '\n': tty_raw(true); return true;
		case 'r': show_regs(m); break;
		case 'd': { if (k < 1) a = m.cpu.reg(7); if (k < 2) n = 16;
			for (unsigned i = 0; i < n; i++) { if (i % 8 == 0) printf("%s%06o:", i ? "\n" : "", (a + 2*i) & 0xffff); printf(" %06o", m.peek16((a + 2*i) & 0xffff)); }
			printf("\n"); break; }
		case 'u': { if (k < 1) a = m.cpu.reg(7); if (k < 2) n = 16; u16 pc = u16(a);
			for (unsigned i = 0; i < n; i++) { int len; std::string s = dasm(m, pc, &len); printf("%06o: %s\n", pc, s.c_str()); pc += len ? len : 2; }
			break; }
		case 's': { if (k < 1) a = 1; for (unsigned i = 0; i < a; i++) { if (m.cpu.halted()) break; m.cpu.run(1); } show_regs(m); break; }
		case 'h': if (line[1] == 'a') { m.cpu.set_halted(true); m.odt_enter(); break; }
			printf("c continue | q quit | r regs | d [addr [n]] dump | u [addr [n]] disasm | s [n] step\n"
			       "ha halt to ODT | R restart (front panel) | g addr go | e addr val poke | t trace on/off\n"); break;
		case 'R': m.restart(); show_regs(m); break;
		case 'g': m.cpu.set_reg(7, u16(a)); m.cpu.set_halted(false); break;
		case 'e': m.poke16(a, u16(n)); break;
		case 't': m.trace = !m.trace; printf("trace %s\n", m.trace ? "on" : "off"); break;
		default: printf("?\n");
		}
	}
}

int main(int argc, char **argv)
{
	std::string roms = "../roms/8550/8501", tracefile, logfile;
	std::string disks[4];
	long long max_cycles = -1;
	bool odt_at_start = false;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "-roms") roms = next();
		else if (a.size() == 2 && a[0] == '-' && a[1] >= '0' && a[1] <= '3') disks[a[1] - '0'] = next();
		else if (a == "-trace") tracefile = next();
		else if (a == "-cycles") max_cycles = atoll(next().c_str());
		else if (a == "-log") logfile = next();
		else if (a == "-odt") odt_at_start = true;
		else { fprintf(stderr, "usage: %s [-roms DIR] [-0..-3 disk] [-trace FILE] [-cycles N] [-log FILE] [-odt]\n", argv[0]); return 1; }
	}

	auto m = std::make_unique<tek8501>();
	std::string err;
	if (!m->load_roms(roms, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	for (int d = 0; d < 4; d++)
		if (!disks[d].empty() && !m->mount(d, disks[d], err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }

	FILE *logf = logfile.empty() ? nullptr : fopen(logfile.c_str(), "wb");
	m->aux.on_tx = [&](u8 c) { c &= 0177; if (c) { fputc(c, stdout); fflush(stdout); } if (logf) { fputc(c, logf); fflush(logf); } };
	m->printer.on_tx = [&](u8 c) { if (logf) { fprintf(logf, "[PRN %03o]", c); fflush(logf); } };

	FILE *tf = tracefile.empty() ? nullptr : fopen(tracefile.c_str(), "w");
	if (tf) m->cpu.trace_hook = [&](u16 pc) { fprintf(tf, "%06o: %s\n", pc, dasm(*m, pc).c_str()); };

	m->power_on();
	if (odt_at_start) { m->cpu.set_halted(true); m->odt_enter(); }
	bool interactive = isatty(0) && max_cycles < 0;
	if (interactive) { tty_raw(true); fprintf(stderr, "[tek8501: Ctrl-] for monitor]\r\n"); }

	using clk = std::chrono::steady_clock;
	auto t0 = clk::now();
	const int slice = int(tek8501::CPU_HZ / 100);    // 10 ms of machine time
	bool was_halted = false;
	for (;;) {
		if (max_cycles >= 0 && (long long)m->cycles() >= max_cycles) break;
		int k;
		while ((k = poll_key()) >= 0) {
			if (k == 035) { if (!monitor(*m)) goto done; continue; }
			if (m->cpu.halted()) m->odt_char(u8(k)); else m->aux.host_rx(u8(k == '\n' ? '\r' : k));
		}
		if (k == -2 && !interactive) {}   // stdin closed
		m->run_slice(slice);
		if (m->cpu.halted() && !was_halted) m->odt_enter();
		was_halted = m->cpu.halted();
		// pace to real time (interactive only)
		if (interactive) {
			auto want = t0 + std::chrono::microseconds(long(m->cycles() / tek8501::CPU_HZ * 1e6));
			if (want > clk::now()) std::this_thread::sleep_until(want);
		}
	}
done:
	tty_raw(false);
	if (!interactive) { fprintf(stderr, "\n"); show_regs(*m); }
	if (tf) fclose(tf);
	if (logf) fclose(logf);
	return 0;
}
