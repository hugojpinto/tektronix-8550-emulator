// CT8500 on a host terminal -- see ct8500_ansi.h
#include "host/ct8500_ansi.h"
#include <cstring>
#include <cstdio>

void ct8500_ansi::key(ct8500 &t, const std::string &k)
{
	if (k.empty()) return;
	using K = ct8500;
	struct map_t { const char *seq; ct8500::key_t key; } static const m[] = {
		{ "\x1b[A", K::K_UP }, { "\x1b[B", K::K_DOWN }, { "\x1b[C", K::K_RIGHT }, { "\x1b[D", K::K_LEFT },
		{ "\x1bOA", K::K_UP }, { "\x1bOB", K::K_DOWN }, { "\x1bOC", K::K_RIGHT }, { "\x1bOD", K::K_LEFT },
		{ "\x1b[H", K::K_HOME }, { "\x1bOH", K::K_HOME }, { "\x1b[1~", K::K_HOME },
		{ "\x1bOP", K::K_F1 }, { "\x1bOQ", K::K_F2 }, { "\x1bOR", K::K_F3 }, { "\x1bOS", K::K_F4 },      // F1-F4
		{ "\x1b[11~", K::K_F1 }, { "\x1b[12~", K::K_F2 }, { "\x1b[13~", K::K_F3 }, { "\x1b[14~", K::K_F4 },
		{ "\x1b[15~", K::K_F5 }, { "\x1b[17~", K::K_F6 }, { "\x1b[18~", K::K_F7 }, { "\x1b[19~", K::K_F8 }, // F5-F8
		{ "\x1b[6~", K::K_F5 }, { "\x1b[5~", K::K_F6 },       // PgDn = F5 (PAGE DOWN), PgUp = F6 (PAGE UP)
		{ "\x1b[2~", K::K_INS_CHAR }, { "\x1b[3~", K::K_DEL_CHAR },
		{ "\x1b[20~", K::K_INS_LINE }, { "\x1b[21~", K::K_DEL_LINE },   // F9/F10
		{ "\x1b[23~", K::K_ERASE_EOL }, { "\x1b[24~", K::K_ERASE_EOP }, // F11/F12
		{ "\x1b[Z", K::K_BACK_TAB },                                    // Shift-Tab
	};
	for (const auto &e : m)
		if (k == e.seq) { t.key(e.key); return; }
	if (k[0] == 0x1b && k.size() > 1) return;             // unknown escape sequence: drop
	for (char ch1 : k) t.type(uint8_t(ch1 == '\n' ? '\r' : ch1));
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
