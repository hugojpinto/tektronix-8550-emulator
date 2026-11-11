// tek8550: the complete 8550 MDL -- 8301 MDU + 8501 DMU joined by the HSI link.
// Your terminal is the 8301 system terminal port (where DOS/50 talks).
//   tek8550 [-0..-3 DISK] [-rw] [-roms DIR] [-sw HEX] [-pm ext] [-ep z80|8085|none] [-ep-prom FILE] [-prom on|fix] [-prom-dev FILE] [-cycles N] [-log FILE] [-hsilog FILE] [-auxlog FILE]
// Ctrl-] enters the monitor ('h' for help).
#include "machine/tek8301.h"
#include "machine/tek8501.h"
#include "cpu/s2650/2650dasm.h"
#include "cpu/t11/t11dasm.h"
#include "host/tty.h"
#include "host/ct8500.h"
#include "host/ipc_target.h"
#include <cstdio>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>

struct cfg_t : s2650_disassembler::config { bool get_z80_mnemonics_mode() const override { return false; } } g_cfg;

static std::string dasm2650(tek8301 &m, u16 pc)
{
	s2650_disassembler d(&g_cfg); data_buffer b; b.rd = [&](offs_t a) -> u8 { return m.read(a & 0x7fff); };
	std::ostringstream s; d.disassemble(s, pc, b, b); return s.str();
}
static std::string dasm11(tek8501 &m, u16 pc)
{
	t11_disassembler d; data_buffer b;
	b.rd = [&](offs_t a) -> u8 { u16 w = m.peek16(a & 0xfffe); return (a & 1) ? u8(w >> 8) : u8(w); };
	std::ostringstream s; d.disassemble(s, pc, b, b); return s.str();
}
static void show(tek8301 &a, tek8501 &b)
{
	printf("8301: PC=%04x R0-3=%02x %02x %02x %02x PSU=%02x PSL=%02x LED=%02x  %s\r\n", a.cpu.pc(), a.cpu.reg(0), a.cpu.reg(1), a.cpu.reg(2), a.cpu.reg(3),
		a.cpu.psu(), a.cpu.psl(), a.leds(), dasm2650(a, a.cpu.pc()).c_str());
	printf("8501: PC=%06o R0-5=%06o %06o %06o %06o %06o %06o SP=%06o PSW=%03o LEDs=%02o %s  %s\r\n", b.cpu.reg(7),
		b.cpu.reg(0), b.cpu.reg(1), b.cpu.reg(2), b.cpu.reg(3), b.cpu.reg(4), b.cpu.reg(5), b.cpu.reg(6), b.cpu.psw(), b.leds(),
		b.cpu.halted() ? "HALTED" : "", dasm11(b, b.cpu.reg(7)).c_str());
	printf("%s\r\n", a.lp_state().c_str());
}

int main(int argc, char **argv)
{
	std::string roms = "../roms/8550", logfile, hsilog, auxlog, dumpfile, termmode, screenfile, targetspec, disks[4];
	long long max_cycles = -1; bool writeback = false; int sw = 0; bool pm_ext = false; std::string ep_prom_file; bool no_ep = false, ep85 = false; bool prom_on = false, prom_fix = false; std::string prom_dev;
	struct typed_t { double at; std::string text; };
	std::vector<typed_t> typed;
	std::vector<typed_t> snaps;      // -snap SECONDS FILE: CT8500 screen state at a machine time
	std::vector<typed_t> pokes;      // -poke SECONDS ADDR=HEXBYTES: write 2650 memory (CPU view) at a machine time
	double trace_at = -1; std::string tracefile;
	double trace11_at = -1, trace11_len = 0; std::string trace11file;   // -trace8501 START SECONDS FILE   // -trace8301 SECONDS FILE (2650 + Z80 instruction trace)      // -type SECONDS "TEXT"  (\r = Return)
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "-roms") roms = next();
		else if (a == "-rw") writeback = true;
		else if (a.size() == 2 && a[0] == '-' && a[1] >= '0' && a[1] <= '3') disks[a[1] - '0'] = next();
		else if (a == "-sw") sw = int(strtol(next().c_str(), nullptr, 16));
		else if (a == "-pm") pm_ext = next() == "ext";
		else if (a == "-ep") { std::string v = next(); no_ep = v == "none" || v == "8085"; ep85 = v == "8085"; }   // -ep z80|8085|none
		else if (a == "-prom-dev") prom_dev = next();     // image of the device in the PROM programmer socket
		else if (a == "-prom") { std::string v = next(); prom_on = v == "on" || v == "fix"; prom_fix = v == "fix"; }   // -prom on|fix
		else if (a == "-ep-prom") ep_prom_file = next();   // Z80 emulator board firmware PROM dump (512 bytes, F800-F9FF)
		else if (a == "-cycles") max_cycles = atoll(next().c_str());
		else if (a == "-log") logfile = next();
		else if (a == "-hsilog") hsilog = next();
		else if (a == "-auxlog") auxlog = next();
		else if (a == "-dump") dumpfile = next();
		else if (a == "-term") termmode = next();
		else if (a == "-target") targetspec = next();
		else if (a == "-snap") { double t = atof(next().c_str()); snaps.push_back({ t, next() }); }
		else if (a == "-poke") { double t = atof(next().c_str()); pokes.push_back({ t, next() }); }
		else if (a == "-screen") screenfile = next();
		else if (a == "-trace8301") { trace_at = atof(next().c_str()); tracefile = next(); }
		else if (a == "-trace8501") { trace11_at = atof(next().c_str()); trace11_len = atof(next().c_str()); trace11file = next(); }
		else if (a == "-type") { double t = atof(next().c_str()); typed.push_back({ t, next() }); }
		else { fprintf(stderr, "usage: %s [-0..-3 DISK] [-rw] [-roms DIR] [-sw HEX] [-pm ext] [-ep z80|8085|none] [-ep-prom FILE] [-prom on|fix] [-prom-dev FILE] [-cycles N] [-log FILE] [-hsilog FILE] [-auxlog FILE] [-dump FILE] [-type SECONDS TEXT] [-term ct8500|tty] [-screen FILE] [-target unix:PATH|tcp:HOST:PORT]\n", argv[0]); return 1; }
	}
	auto mdu = std::make_unique<tek8301>();
	auto dmu = std::make_unique<tek8501>();
	std::string err;
	if (!mdu->load_roms(roms + "/8301", err) || !dmu->load_roms(roms + "/8501", err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	for (int d = 0; d < 4; d++) {
		if (disks[d].empty()) continue;
		// -rw writes back every mounted image; originals under sources/ stay read-only
		bool wb = writeback && disks[d].find("sources/") == std::string::npos;
		if (writeback && !wb) fprintf(stderr, "[drive %d: %s is an original; mounted without write-back]\n", d, disks[d].c_str());
		if (!dmu->mount(d, disks[d], err, wb)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	}

	FILE *logf = logfile.empty() ? nullptr : fopen(logfile.c_str(), "wb");
	FILE *hsif = hsilog.empty() ? nullptr : fopen(hsilog.c_str(), "w");
	FILE *auxf = auxlog.empty() ? nullptr : fopen(auxlog.c_str(), "wb");
	bool interactive = isatty(0) && max_cycles < 0;
	// terminal: CT8500 model rendered with ANSI (default when interactive), or raw pass-through
	if (termmode.empty()) termmode = (interactive && isatty(1)) ? "ct8500" : "tty";
	const bool ct_mode = termmode == "ct8500";
	ct8500 ct;
	bool ct_dirty = false;
	ct.send = [&](u8 b) { mdu->term.host_rx(b); };
	mdu->term.on_tx = [&](u8 c) {
		c &= 0x7f;
		if (ct_mode || !screenfile.empty() || !snaps.empty()) { ct.feed(c); ct_dirty = true; }
		if (!ct_mode && c) { fputc(c, stdout); fflush(stdout); }
		if (logf) { fputc(c, logf); fflush(logf); }
	};
	dmu->aux.on_tx = [&](u8 c) { if (auxf) { fputc(c & 0x7f, auxf); fflush(auxf); } };

	// the HSI cable
	int last_dir = 0;
	auto hlog = [&](int dir, u8 c) {
		if (!hsif) return;
		if (dir != last_dir) { fprintf(hsif, "\n%s", dir == 1 ? "8301>" : "8501>"); last_dir = dir; }
		fprintf(hsif, " %02x", c); fflush(hsif);
	};
	mdu->hsi_connected = true;
	mdu->hsi_tx = [&](u8 c) { hlog(1, c); dmu->hsi_rx(c); };
	dmu->hsi_tx = [&](u8 c) { hlog(2, c); mdu->hsi_rx(c); };
	mdu->hsi_cts = [&]() { return !dmu->hsi_ncts(); };
	dmu->hsi_peer_ready = [&]() { return !mdu->hsi_rx_busy(); };

	ipc_target proto;
	if (!targetspec.empty()) {
		if (!proto.open(targetspec, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
		mdu->target = &proto;
	}
	mdu->mode_switch = u8(sw);
	mdu->pm_ext_comparator = pm_ext;
	if (no_ep) mdu->ep_present = false;
	mdu->ep85_present = ep85;
	mdu->prom.present = prom_on;
	mdu->prom_dos50_fix = prom_fix;
	if (!prom_dev.empty()) { std::string err; mdu->prom.load_socket(prom_dev, err); }
	else mdu->prom.socket.assign(0x10000, 0xff);
	if (!ep_prom_file.empty()) {
		FILE *f = fopen(ep_prom_file.c_str(), "rb");
		if (!f) { fprintf(stderr, "cannot read %s\n", ep_prom_file.c_str()); return 1; }
		mdu->ep_prom.resize(512); size_t n = fread(mdu->ep_prom.data(), 1, 512, f); fclose(f);
		if (n != 512) { fprintf(stderr, "%s: expected 512 bytes, got %zu\n", ep_prom_file.c_str(), n); return 1; }
	}
	mdu->trace_io = getenv("TEK_IOTRACE8301") != nullptr;
	mdu->ep_trace = getenv("TEK_EPTRACE") != nullptr;
	FILE *trf = tracefile.empty() ? nullptr : fopen(tracefile.c_str(), "w");
	if (trf) {
		mdu->cpu.trace_hook = [&](u16 pc) {
			if (mdu->cycles() < trace_at * tek8301::CPU_HZ) return;
			auto &c = mdu->cpu;
			fprintf(trf, "%04x: %-20s r0-3=%02x %02x %02x %02x psu=%02x t=%.4f\n", pc, dasm2650(*mdu, pc).c_str(), c.reg(0), c.reg(1), c.reg(2), c.reg(3), c.psu(), mdu->cycles() / tek8301::CPU_HZ);
		};
		mdu->lp_trace_hook = [&](u16 pc) {
			if (mdu->cycles() < trace_at * tek8301::CPU_HZ) return;
			fprintf(trf, "  Z80 %04x\n", pc);
		};
	}
	FILE *trf11 = trace11file.empty() ? nullptr : fopen(trace11file.c_str(), "w");
	if (trf11) dmu->cpu.trace_hook = [&](u16 pc) {
		double t = dmu->cycles() / tek8501::CPU_HZ;
		if (t >= trace11_at && t < trace11_at + trace11_len) fprintf(trf11, "%06o: %s\n", pc, dasm11(*dmu, pc).c_str());
	};
	mdu->power_on();
	dmu->power_on();

	if (interactive) {
		host::tty_raw(true);
		if (ct_mode) { fputs(ct.enter_screen().c_str(), stdout); fflush(stdout); }
		else fprintf(stderr, "[tek8550: Ctrl-] for monitor]\r\n");
	}
	std::string keybuf;
	using clk = std::chrono::steady_clock;
	auto t0 = clk::now();
	const double slice_s = 0.0001;   // run both machines in 100 us lockstep
	u64 ticks = 0;
	for (;;) {
		if (max_cycles >= 0 && (long long)mdu->cycles() >= max_cycles) break;
		int k;
		while ((k = host::poll_key()) >= 0) keybuf += char(k);
		while (!keybuf.empty()) {
			u8 k0 = u8(keybuf[0]);
			if (k0 == 035) {
				keybuf.erase(0, 1);
				if (ct_mode) { fputs(ct.leave_screen().c_str(), stdout); fflush(stdout); }
				host::tty_raw(false);
				printf("\n[monitor] c continue | q quit | r regs | R restart both\n"); show(*mdu, *dmu);
				char line[64]; bool quit = false;
				for (;;) { printf("mon> "); fflush(stdout); if (!fgets(line, sizeof line, stdin)) { quit = true; break; }
					if (line[0] == 'q') { quit = true; break; } if (line[0] == 'c' || line[0] == '\n') break;
					if (line[0] == 'r') show(*mdu, *dmu); if (line[0] == 'R') { mdu->restart(); dmu->restart(); } }
				if (quit) goto done;
				host::tty_raw(true);
				if (ct_mode) { fputs(ct.enter_screen().c_str(), stdout); fputs(ct.render(true).c_str(), stdout); fflush(stdout); }
				continue;
			}
			// split off one key: ESC [ ... final, ESC O x, lone ESC, or a plain byte
			size_t n = 1;
			if (k0 == 0x1b && keybuf.size() >= 2 && (keybuf[1] == '[' || keybuf[1] == 'O')) {
				if (keybuf[1] == 'O') n = std::min<size_t>(3, keybuf.size());
				else { n = 2; while (n < keybuf.size() && !(keybuf[n] >= 0x40 && keybuf[n] <= 0x7e)) n++; n = std::min(n + 1, keybuf.size()); }
			}
			std::string key = keybuf.substr(0, n); keybuf.erase(0, n);
			if (ct_mode) ct.key(key);
			else for (char ch1 : key) mdu->term.host_rx(u8(ch1 == '\n' ? '\r' : ch1));
		}
		for (auto &t : typed)
			if (!t.text.empty() && mdu->cycles() >= t.at * tek8301::CPU_HZ) {
				for (size_t i = 0; i < t.text.size(); i++) {
					char ch = t.text[i];
					if (ch == '\\' && i + 1 < t.text.size()) {
						char e = t.text[++i];
						if (e == 'r') ch = '\r'; else if (e == 'n') ch = '\n'; else if (e == 'e') ch = 27;
						else if (e == 'x' && i + 2 < t.text.size()) { ch = char(strtol(t.text.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
						else ch = e;
					}
					mdu->term.host_rx(u8(ch));
				}
				t.text.clear();
			}
		for (auto &pk : pokes)
			if (!pk.text.empty() && mdu->cycles() >= pk.at * tek8301::CPU_HZ) {
				size_t eq = pk.text.find('=');
				unsigned addr = unsigned(strtoul(pk.text.substr(0, eq).c_str(), nullptr, 16));
				for (size_t i = eq + 1; i + 1 < pk.text.size() + 1 && i + 2 <= pk.text.size(); i += 2)
					mdu->poke(addr++, u8(strtoul(pk.text.substr(i, 2).c_str(), nullptr, 16)));
				pk.text.clear();
			}
		for (auto &sn : snaps)
			if (!sn.text.empty() && mdu->cycles() >= sn.at * tek8301::CPU_HZ) {
				FILE *f = fopen(sn.text.c_str(), "w"); if (f) { fputs(ct.state().c_str(), f); fclose(f); }
				sn.text.clear();
			}
		mdu->run_slice(int(tek8301::CPU_HZ * slice_s));
		dmu->run_slice(int(tek8501::CPU_HZ * slice_s));
		if (dmu->cpu.halted()) dmu->cpu.set_halted(false);   // no ODT console here: resume (logged)
		ticks++;
		if (ct_mode && interactive && ct_dirty && ticks % 20 == 0) { fputs(ct.render().c_str(), stdout); fflush(stdout); ct_dirty = false; }
		if (ticks % 1000 == 0) { std::string e; if (!dmu->flush(e)) fprintf(stderr, "[disk save failed: %s]\r\n", e.c_str()); }
		if (interactive && ticks % 10 == 0) {
			auto want = t0 + std::chrono::microseconds(ticks * 100);
			if (want > clk::now()) std::this_thread::sleep_until(want);
		}
	}
done:
	if (ct_mode && interactive) { fputs(ct.leave_screen().c_str(), stdout); fflush(stdout); }
	host::tty_raw(false);
	if (!screenfile.empty()) { FILE *f = fopen(screenfile.c_str(), "w"); if (f) { fputs(ct.text().c_str(), f); fclose(f); } }
	if (!targetspec.empty()) fprintf(stderr, "[prototype link: %lu requests, %lu WAIT states]\n", proto.requests(), proto.wait_total());
	{ std::string e; if (!dmu->flush(e, true)) fprintf(stderr, "disk save failed: %s\n", e.c_str()); }
	{ std::string e; if (!mdu->prom.save_socket(e)) fprintf(stderr, "PROM image save failed: %s\n", e.c_str()); }
	if (!interactive) { fprintf(stderr, "\n"); show(*mdu, *dmu); }
	for (FILE *f : { logf, hsif, auxf, trf, trf11 }) if (f) fclose(f);
	if (!dumpfile.empty()) {   // raw memory images: <file>.8501 (64 KB, LE words) and <file>.8301 (CPU view, 32 KB)
		FILE *f = fopen((dumpfile + ".8501").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a += 2) { u16 w = dmu->peek16(a); fputc(w & 0xff, f); fputc(w >> 8, f); }
		fclose(f);
		f = fopen((dumpfile + ".8301").c_str(), "wb");
		for (u32 a = 0; a < 0x8000; a++) fputc(mdu->read(a), f);
		fclose(f);
		f = fopen((dumpfile + ".sys").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a++) fputc(mdu->sysmem(u16(a)), f);
		fclose(f);
		f = fopen((dumpfile + ".prog").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a++) fputc(mdu->progmem(u16(a)), f);
		fclose(f);
	}
	return 0;
}
