// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// tek8550 command-line options, shared by every front end (tty, SDL).
#ifndef TEK_SYSTEM_OPTIONS_H
#define TEK_SYSTEM_OPTIONS_H
#include <string>
#include <vector>

struct tek8550_options
{
	struct timed_t { double at; std::string text; };

	std::string roms = "../roms/8550";
	std::string disks[4];
	bool writeback = false;            // -rw
	int sw = 0;                        // -sw HEX: 8301 mode DIP switch
	bool pm_ext = false;               // -pm ext
	bool no_ep = false, ep85 = false, ep86 = false, ep88 = false, ep186 = false;  // -ep z80|8085|8086|8088|80186|80188|none
	std::string ep_prom_file;          // -ep-prom
	bool prom_on = false, prom_fix = false; std::string prom_dev;
	bool rtpa_on = false, rtpa_probe = false, tta_on = false, tta_probe = false;
	bool fdc_hle = false;               // -fdc hle|lle: flexible disc controller model
	long long max_cycles = -1;         // -cycles
	std::string logfile, hsilog, auxlog, dumpfile, outputs_log;
	std::string termmode;              // -term ct8500|tty (front-end specific)
	std::string screenfile;            // -screen
	std::string targetspec, remotespec; int remote_baud = 2400;
	std::string aux8301spec, aux8501spec, printerspec;   // -aux8301, -aux8501, -printer (serial_link specs)
	std::vector<timed_t> typed;        // -type SECONDS TEXT (\r = Return)
	std::vector<timed_t> snaps;        // -snap SECONDS FILE
	std::vector<timed_t> pokes;        // -poke SECONDS ADDR=HEXBYTES
	std::vector<timed_t> switches;     // -input SECONDS NAME=VALUE, -press SECONDS NAME (text without '=')
	std::vector<timed_t> discs;        // -mount SECONDS N=FILE, -eject SECONDS N (text "N" alone)
	double trace_at = -1; std::string tracefile;                          // -trace8301 SECONDS FILE
	double trace11_at = -1, trace11_len = 0; std::string trace11file;    // -trace8501 START SECONDS FILE

	// returns false on an unknown option (unknown = the offending argument)
	bool parse(int argc, char **argv, std::string &unknown);
	static const char *usage();
};
#endif
