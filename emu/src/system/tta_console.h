// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The TTA command set (Trigger Trace Analyzer users manual 070-3760-01, section 2): acq,
// ad, bre, bus, cons, cou, ctr, data, disp, eve, pro, qua, tclr, ts. DOS/50 V1.0 predates
// the TTA, and the later DOS/50 that had these commands does not survive, so the emulator
// runs them itself: lines typed at the DOS/50 prompt whose first word is one of them come
// here instead (tek8550_system::key_in). The commands program the board's run mode
// (machine/tta_run.h) and print as the manual shows.
#ifndef TEK_SYSTEM_TTA_CONSOLE_H
#define TEK_SYSTEM_TTA_CONSOLE_H
#include "machine/tta_run.h"
#include <string>
#include <vector>

class tek8550_system;

class tta_console
{
public:
	explicit tta_console(tek8550_system &s);
	std::string run(const std::string &line);     // one command line; returns what it prints
	static bool is_command(const std::string &word);
	void run_started() { m_header = false; }      // the next trace line gets its header
private:
	tek8550_system &sys;
	bool m_header = false;
	std::string command(const std::string &line);
	std::string status(u8 which, bool events, bool counters) const;
	std::string display(const std::vector<tta_run::sample> &v) const;
	std::string cons_text() const;
	std::string acq_text(bool with_remaining) const;
	std::string show_bre() const;
	void trace_line();
};
#endif
