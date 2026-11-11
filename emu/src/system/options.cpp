// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
#include "system/options.h"
#include <cstdlib>

const char *tek8550_options::usage()
{
	return "[-0..-3 DISK] [-rw] [-roms DIR] [-sw HEX] [-pm ext|32k|ext,32k] [-ep z80|8085|8086|8088|80186|80188|none] [-ep-prom FILE] [-prom on|fix] [-prom-dev FILE] [-cycles N] [-log FILE] [-hsilog FILE] [-auxlog FILE] [-outputs FILE] [-dump FILE] [-type SECONDS TEXT] [-snap SECONDS FILE] [-poke SECONDS ADDR=HEX] [-mount SECONDS N=FILE] [-eject SECONDS N] [-input SECONDS NAME=VALUE] [-press SECONDS NAME] [-term ct8500|tty] [-screen FILE] [-target unix:PATH|tcp:HOST:PORT] [-remote tcp:HOST:PORT|listen:PORT|unix:PATH] [-remote-baud N] [-aux8301 SPEC] [-aux8501 SPEC] [-printer SPEC] [-rtpa [probe]] [-tta [probe]] [-fdc lle|hle] [-trace8301 SECONDS FILE] [-trace8501 START SECONDS FILE]";
}

bool tek8550_options::parse(int argc, char **argv, std::string &unknown)
{
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "-roms") roms = next();
		else if (a == "-rw") writeback = true;
		else if (a.size() == 2 && a[0] == '-' && a[1] >= '0' && a[1] <= '3') disks[a[1] - '0'] = next();
		else if (a == "-sw") sw = int(strtol(next().c_str(), nullptr, 16));
		else if (a == "-pm") {             // ext: boards with the extended-bank comparator; 32k: one board, relocation jumper on
			std::string v = next(), t;
			for (size_t i = 0; i <= v.size(); i++) {
				if (i == v.size() || v[i] == ',') { if (t == "ext") pm_ext = true; else if (t == "32k") pm_32k = true; t.clear(); }
				else t += v[i];
			}
		}
		else if (a == "-ep") { std::string v = next(); no_ep = v != "z80"; ep85 = v == "8085"; ep86 = v == "8086" || v == "8088" || v == "80186" || v == "80188"; ep88 = v == "8088" || v == "80188"; ep186 = v == "80186" || v == "80188"; }
		else if (a == "-prom-dev") prom_dev = next();     // image of the device in the PROM programmer socket
		else if (a == "-prom") { std::string v = next(); prom_on = v == "on" || v == "fix"; prom_fix = v == "fix"; }
		else if (a == "-ep-prom") ep_prom_file = next();   // Z80 emulator board firmware PROM dump (512 bytes, F800-F9FF)
		else if (a == "-cycles") max_cycles = atoll(next().c_str());
		else if (a == "-log") logfile = next();
		else if (a == "-hsilog") hsilog = next();
		else if (a == "-auxlog") auxlog = next();
		else if (a == "-outputs") outputs_log = next();
		else if (a == "-dump") dumpfile = next();
		else if (a == "-term") termmode = next();
		else if (a == "-target") targetspec = next();
		else if (a == "-remote") remotespec = next();
		else if (a == "-aux8301") aux8301spec = next();
		else if (a == "-aux8501") aux8501spec = next();
		else if (a == "-printer") printerspec = next();
		else if (a == "-tta") { tta_on = true; if (i + 1 < argc && std::string(argv[i + 1]) == "probe") { i++; tta_probe = true; } }
		else if (a == "-fdc") fdc_hle = next() == "hle";
		else if (a == "-rtpa") { rtpa_on = true; if (i + 1 < argc && std::string(argv[i + 1]) == "probe") { i++; rtpa_probe = true; } }
		else if (a == "-remote-baud") remote_baud = atoi(next().c_str());
		else if (a == "-snap") { double t = atof(next().c_str()); snaps.push_back({ t, next() }); }
		else if (a == "-input") { double t = atof(next().c_str()); switches.push_back({ t, next() }); }
		else if (a == "-press") { double t = atof(next().c_str()); std::string n = next(); if (n.find('=') != std::string::npos) { unknown = a + " " + n; return false; } switches.push_back({ t, n }); }
		else if (a == "-poke") { double t = atof(next().c_str()); pokes.push_back({ t, next() }); }
		else if (a == "-mount") { double t = atof(next().c_str()); discs.push_back({ t, next() }); }
		else if (a == "-eject") { double t = atof(next().c_str()); discs.push_back({ t, next() }); }
		else if (a == "-screen") screenfile = next();
		else if (a == "-trace8301") { trace_at = atof(next().c_str()); tracefile = next(); }
		else if (a == "-trace8501") { trace11_at = atof(next().c_str()); trace11_len = atof(next().c_str()); trace11file = next(); }
		else if (a == "-type") { double t = atof(next().c_str()); typed.push_back({ t, next() }); }
		else { unknown = a; return false; }
	}
	return true;
}
