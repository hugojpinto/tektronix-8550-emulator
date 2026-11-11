// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// CT8500 on a host terminal: ANSI rendering of the model and the host keyboard's
// escape sequences mapped to CT8500 keys (tty front end).
#ifndef TEK_HOST_CT8500_ANSI_H
#define TEK_HOST_CT8500_ANSI_H
#include "host/ct8500.h"
#include <string>

class ct8500_ansi
{
public:
	// ANSI output that brings the user's terminal up to date
	std::string render(ct8500 &t, bool full = false);
	static std::string enter_screen() { return "\x1b[?1049h\x1b[2J"; }
	static std::string leave_screen() { return "\x1b[0m\x1b[?25h\x1b[?1049l"; }
	// a key or ANSI escape sequence from the user's terminal
	static void key(ct8500 &t, const std::string &seq);
private:
	uint8_t sh_ch[ct8500::VIEW][ct8500::COLS], sh_at[ct8500::VIEW][ct8500::COLS];
	bool sh_valid = false; int sh_top = -1, sh_r = -1, sh_c = -1; bool sh_cur = true;
};
#endif
