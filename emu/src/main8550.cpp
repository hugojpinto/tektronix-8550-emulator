// tek8550: the complete 8550 MDL -- tty front end.
// Your terminal is the 8301 system terminal port (where DOS/50 talks): either the
// CT8500 model rendered with ANSI (default when interactive) or raw pass-through.
// The machine itself lives in system/tek8550.*; options are in system/options.*.
// Ctrl-] enters the monitor ('h' for help).
#include "system/tek8550.h"
#include "host/tty.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

static void monitor_help()
{
	printf("[monitor] c continue | q quit | r regs | R restart both | o outputs | p NAME [VALUE] input (8301:restart, 8501:restart, 8301:modesw) | m N FILE mount | e N eject\n");
}

int main(int argc, char **argv)
{
	tek8550_options opt;
	std::string bad;
	if (!opt.parse(argc, argv, bad)) { fprintf(stderr, "usage: %s %s\n", argv[0], tek8550_options::usage()); return 1; }

	bool interactive = isatty(0) && opt.max_cycles < 0;
	std::string termmode = opt.termmode;
	if (termmode.empty()) termmode = (interactive && isatty(1)) ? "ct8500" : "tty";
	const bool ct_mode = termmode == "ct8500";

	tek8550_system sys(opt);
	sys.on_term_tx = [&](u8 c) { if (!ct_mode && c) { fputc(c, stdout); fflush(stdout); } };
	std::string err;
	if (!sys.start(ct_mode, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
	ct8500 &ct = sys.terminal();

	if (interactive) {
		host::tty_raw(true);
		if (ct_mode) { fputs(ct.enter_screen().c_str(), stdout); fflush(stdout); }
		else fprintf(stderr, "[tek8550: Ctrl-] for monitor]\r\n");
	}
	std::string keybuf;
	using clk = std::chrono::steady_clock;
	auto t0 = clk::now();
	while (!sys.finished()) {
		int k;
		while ((k = host::poll_key()) >= 0) keybuf += char(k);
		while (!keybuf.empty()) {
			u8 k0 = u8(keybuf[0]);
			if (k0 == 035) {
				keybuf.erase(0, 1);
				if (ct_mode) { fputs(ct.leave_screen().c_str(), stdout); fflush(stdout); }
				host::tty_raw(false);
				printf("\n"); monitor_help(); sys.show(stdout);
				char line[128]; bool quit = false;
				for (;;) {
					printf("mon> "); fflush(stdout);
					if (!fgets(line, sizeof line, stdin)) { quit = true; break; }
					if (line[0] == 'q') { quit = true; break; }
					if (line[0] == 'c' || line[0] == '\n') break;
					if (line[0] == 'h') monitor_help();
					if (line[0] == 'r') sys.show(stdout);
					if (line[0] == 'R') sys.input("restart");
					if (line[0] == 'o') for (auto &kv : sys.out.all()) printf("  %-16s %d\n", kv.first.c_str(), kv.second);
					if (line[0] == 'm' || line[0] == 'e') {
						char path[1024] = ""; int n = -1; std::string e;
						int got = sscanf(line + 1, "%d %1023[^\n]", &n, path);
						if (n < 0 || n > 3 || (line[0] == 'm' && got < 2)) printf("usage: m N FILE | e N\n");
						else if (!(line[0] == 'm' ? sys.mount(n, path, e) : sys.eject(n, e))) printf("%s\n", e.c_str());
					}
					if (line[0] == 'p') {
						char name[64]; int v = 1;
						if (sscanf(line + 1, "%63s %i", name, &v) >= 1 && !sys.input(name, v)) printf("unknown input %s\n", name);
					}
				}
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
			else for (char ch1 : key) sys.term_rx(u8(ch1 == '\n' ? '\r' : ch1));
		}
		sys.tick();
		u64 ticks = sys.ticks();
		if (ct_mode && interactive && sys.terminal_dirty && ticks % 20 == 0) { fputs(ct.render().c_str(), stdout); fflush(stdout); sys.terminal_dirty = false; }
		if (interactive && ticks % 10 == 0) {
			auto want = t0 + std::chrono::microseconds(ticks * 100);
			if (want > clk::now()) std::this_thread::sleep_until(want);
		}
	}
done:
	if (ct_mode && interactive) { fputs(ct.leave_screen().c_str(), stdout); fflush(stdout); }
	host::tty_raw(false);
	sys.shutdown(!interactive);
	return 0;
}
