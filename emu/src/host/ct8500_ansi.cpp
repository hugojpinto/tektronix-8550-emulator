// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// CT8500 on a host terminal -- see ct8500_ansi.h
#include "host/ct8500_ansi.h"
#include <cstring>
#include <cstdio>

// The host terminal's keys, as xterm-style sequences (with modifiers: ;2 Shift, ;5 Ctrl,
// ;6 Ctrl+Shift), mapped like the GUI's keyboard (emu/README.md)
void ct8500_ansi::key(ct8500 &t, const std::string &k)
{
	if (k.empty()) return;
	using K = ct8500;
	if (k[0] != 0x1b || k.size() < 2) { for (char ch1 : k) t.type(uint8_t(ch1 == '\n' ? '\r' : ch1)); return; }
	// ESC O x (SS3) or ESC [ params final (CSI)
	int p1 = 0, mod = 1; char fin = 0;
	if (k[1] == 'O' && k.size() >= 3) fin = k[2];
	else if (k[1] == '[') {
		size_t i = 2; int *cur = &p1;
		for (; i < k.size(); i++) {
			char ch1 = k[i];
			if (ch1 >= '0' && ch1 <= '9') *cur = *cur * 10 + (ch1 - '0');
			else if (ch1 == ';') { mod = 0; cur = &mod; }
			else { fin = ch1; break; }
		}
	}
	if (!fin) return;                                              // a lone ESC prefix or unknown: drop
	if (mod <= 0) mod = 1;
	const bool sh = (mod - 1) & 1, cn = (mod - 1) & 4;
	auto key = [&](K::key_t kk) { t.key(kk, sh, cn); };
	switch (fin) {
	case 'A': key(K::K_UP); return;
	case 'B': key(K::K_DOWN); return;
	case 'C': key(K::K_RIGHT); return;
	case 'D': key(K::K_LEFT); return;
	case 'H': key(K::K_HOME); return;
	case 'F': key(K::K_BREAK); return;                             // End: BREAK (Shift/Ctrl: SEND)
	case 'Z': t.key(K::K_TAB, true); return;                       // Shift-Tab: TAB BACK
	case 'P': key(K::K_F1); return;
	case 'Q': key(K::K_F2); return;
	case 'R': key(K::K_F3); return;
	case 'S': key(K::K_F4); return;
	case '~':
		switch (p1) {
		case 1: case 7: key(K::K_HOME); return;
		case 4: case 8: key(K::K_BREAK); return;                   // End
		case 2: key(K::K_INS_CHAR); return;                        // Insert (Shift: DELETE CHAR)
		case 3: t.key(K::K_INS_CHAR, true); return;                // Delete: DELETE CHAR
		case 5: t.key(K::K_F5); return;                            // PgUp: PAGE (Page One)
		case 6: t.key(K::K_F6); return;                            // PgDn: PAGE (Page Two)
		case 11: key(K::K_F1); return;
		case 12: key(K::K_F2); return;
		case 13: key(K::K_F3); return;
		case 14: key(K::K_F4); return;
		case 15: key(K::K_F5); return;
		case 17: key(K::K_F6); return;
		case 18: key(K::K_F7); return;
		case 19: key(K::K_F8); return;
		case 20: key(K::K_ERASE_EOL); return;                      // F9  (Shift: ERASE TO EOP)
		case 21: key(K::K_CLEAR_SCREEN); return;                   // F10 (Shift: CLEAR ALL)
		case 23: key(K::K_TAB_SET); return;                        // F11 (Shift: TAB CLEAR)
		case 24: key(K::K_INS_LINE); return;                       // F12 (Shift: DELETE LINE)
		default: return;
		}
	default: return;
	}
}

std::string ct8500_ansi::render(ct8500 &t, bool full)
{
	using A = ct8500;
	const int VIEW = ct8500::VIEW, COLS = ct8500::COLS;
	std::string o;
	if (!sh_valid || full) { o += "\x1b[0m\x1b[2J"; memset(sh_ch, 0, sizeof sh_ch); memset(sh_at, 0xff, sizeof sh_at); sh_valid = true; }
	uint8_t cur = 0xff;
	auto sgr = [&](uint8_t a) {
		if (a == cur) return;
		o += "\x1b[0";
		if (a & A::A_UL) o += ";4";
		if (a & A::A_BLINK) o += ";5";
		if (a & A::A_INV) o += ";7";
		if (a & A::A_DIM) o += ";2";
		o += "m"; cur = a;
	};
	bool moved = false;
	for (int i = 0; i < VIEW; i++) {
		uint8_t rc[COLS], ra[COLS];
		for (int j = 0; j < COLS; j++) { rc[j] = t.cell(i, j); ra[j] = t.attrs(i, j); }
		if (!memcmp(rc, sh_ch[i], COLS) && !memcmp(ra, sh_at[i], COLS)) continue;
		// repaint from the first differing column to the end of the row
		int f = 0; while (f < COLS && rc[f] == sh_ch[i][f] && ra[f] == sh_at[i][f]) f++;
		char buf[24]; snprintf(buf, sizeof buf, "\x1b[%d;%dH", i + 1, f + 1); o += buf;
		for (int j = f; j < COLS; j++) {
			sgr(ra[j] & ~A::A_BLANK);
			o += (ra[j] & A::A_BLANK) ? ' ' : char(rc[j] >= 0x20 && rc[j] < 0x7f ? rc[j] : ' ');
		}
		memcpy(sh_ch[i], rc, COLS); memcpy(sh_at[i], ra, COLS);
		moved = true;
	}
	if (cur != 0xff) o += "\x1b[0m";
	int r = t.cursor_row() + t.top_line(), c = t.cursor_col(), top = t.top_line();
	if (moved || r != sh_r || c != sh_c || top != sh_top) {
		char buf[24]; snprintf(buf, sizeof buf, "\x1b[%d;%dH", r - top + 1, c + 1); o += buf;
		sh_r = r; sh_c = c; sh_top = top;
	}
	if (t.take_bell()) o += '\a';
	if (t.cursor_visible() != sh_cur) { o += t.cursor_visible() ? "\x1b[?25h" : "\x1b[?25l"; sh_cur = t.cursor_visible(); }
	return o;
}
