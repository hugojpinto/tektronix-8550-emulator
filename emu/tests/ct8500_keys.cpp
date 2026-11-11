// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// CT8500 keyboard behaviour against the Operators Manual 070-3737 (§1 "The Terminal
// Keyboard", §2 procedures). Run with "make test".
#include "host/ct8500.h"
#include <cstdio>
#include <string>
#include <algorithm>

static int fails = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }

struct term {
	ct8500 t; std::string sent; int breaks = 0;
	term() { t.send = [this](uint8_t b) { sent += char(b); }; t.send_break = [this]() { breaks++; }; }
	void host(const std::string &s) { for (char c : s) t.feed(uint8_t(c)); }
	std::string take() { std::string s = sent; sent.clear(); return s; }
};

int main()
{
	using K = ct8500;
	{   // power-up: characters go to the host, editing keys act locally
		term x;
		x.t.type('a'); check(x.take() == "a", "power-up: a typed character is sent");
		x.t.key(K::K_TTY_LOCK); x.t.type('a'); check(x.take() == "A", "TTY LOCK sends upper case");
		x.t.key(K::K_INS_LINE); check(x.take().empty(), "power-up: INSERT LINE acts locally, sends nothing");
		x.t.key(K::K_F6); check(x.t.top_line() == 25, "power-up: F6 PAGE shows Page Two");
		x.t.key(K::K_F5); check(x.t.top_line() == 0 && x.t.cursor_row() == 0, "power-up: F5 PAGE shows Page One, cursor home");
		x.t.key(K::K_UP); check(x.t.cursor_row() == 24, "cursor up from the top line goes to the bottom line");
		x.t.key(K::K_HOME); x.t.key(K::K_LEFT); check(x.t.cursor_row() == 24 && x.t.cursor_col() == 79, "cursor left from home goes to the end of the bottom line");
		x.t.key(K::K_HOME, true, true); check(x.t.top_line() == 25 && x.t.cursor_row() == 0, "CNTRL-SHIFT-HOME: home of Page Two");
		x.t.key(K::K_HOME, false, true); check(x.t.top_line() == 0, "CNTRL-HOME: home of Page One");
		x.t.key(K::K_BREAK); check(x.breaks == 1, "BREAK puts a break on the line");
	}
	{   // host-control: every key sends ESC + its code
		term x; x.host("\x1b\x3d");
		x.t.key(K::K_INS_LINE); check(x.take() == "\x1b\x0c", "host-control: INSERT LINE sends ESC 0C");
		x.t.key(K::K_INS_LINE, true); check(x.take() == "\x1b\x0d", "host-control: shifted (DELETE LINE) sends ESC 0D");
		x.t.key(K::K_F5); check(x.take() == "\x1b\x4c", "host-control: F5 sends ESC 4C (Table 2-2)");
		x.t.key(K::K_UP); check(x.take() == "\x1b\x52", "host-control: cursor up sends ESC 52");
		x.t.key(K::K_ERASE_EOL, true); check(x.take() == "\x1b\x15", "host-control: ERASE TO EOP sends ESC 15");
	}
	{   // local: typed text is displayed and not sent; SEND transmits a line
		term x; x.t.key(K::K_LOCAL);
		x.t.type('H'); x.t.type('I');
		check(x.take().empty() && x.t.cell(0, 0) == 'H' && x.t.cell(0, 1) == 'I', "local: typed text is displayed, not sent");
		x.t.key(K::K_BREAK); check(x.breaks == 0, "local: BREAK does nothing");
		x.t.key(K::K_BREAK, true); check(x.take() == "HI\r", "SHIFT-SEND transmits the cursor line");
		check(x.t.mode() == K::POWERUP, "after SEND the terminal is back on line");
		x.t.key(K::K_LOCAL); x.t.key(K::K_REMOTE); check(x.t.mode() == K::POWERUP, "REMOTE returns to power-up mode");
	}
	{   // remote learn, the manual's example: SHIFT-F3 = home, transmit page, clear screen
		term x;
		x.host("\x1b\x39\x1b\x4a\x1b\x54\x1b\x02\x1b\x04\x1f");
		check(x.t.fkey_string(2) == std::string("\x1bT\x1b\x02\x1b\x04"), "remote learn stores SHIFT-F3's string");
		x.host("ONE\r\nTWO");
		x.t.key(K::K_F3, true);
		std::string page = x.take();
		check(page.compare(0, 8, "ONE\rTWO\r") == 0 && std::count(page.begin(), page.end(), '\r') == 25,
			"SHIFT-F3 plays: home, transmit page (25 display lines, each ending in CR)");
		check(x.t.cell(0, 0) == ' ' && x.t.cell(1, 0) == ' ', "... then clears the screen");
	}
	{   // local learn: CNTRL-SHIFT-F2, SHIFT-F1, the text, CNTRL-SHIFT-F3
		term x;
		x.t.key(K::K_F2, true, true); check(x.t.learning(), "CNTRL-SHIFT-F2 enters learn mode");
		x.t.key(K::K_F1, true); x.t.type('D'); x.t.type('S'); x.t.type('\r');
		x.t.key(K::K_F3, true, true); check(!x.t.learning() && x.take().empty(), "CNTRL-SHIFT-F3 leaves learn mode; nothing was sent");
		x.t.key(K::K_F1, true); check(x.take() == "DS\r", "SHIFT-F1 now types DS<CR>");
		x.t.key(K::K_F6, true, true); check(x.t.mode() == K::HOST, "CNTRL-SHIFT-F6 enters host-control mode");
	}
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails;
}
