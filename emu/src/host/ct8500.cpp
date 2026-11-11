// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix CT8500 model -- see ct8500.h
#include "host/ct8500.h"
#include <cstring>
#include <cstdio>

void ct8500::clear_all()
{
	memset(ch, ' ', sizeof ch); memset(at, 0, sizeof at);
	r = c = 0; top = 0; attr = 0;
}

void ct8500::clear_rows(int from, int to)
{
	for (int i = from; i <= to && i < ROWS; i++) { memset(ch[i], ' ', COLS); memset(at[i], 0, COLS); }
}

void ct8500::roll_up()
{
	// auto roll-up: lines above the split are protected; when split is in Page Two
	// all of Page One is protected too (070-3737 "Dividing the Screen Horizontally")
	int first = split;
	if (first < 0) first = 0;
	for (int i = first; i < ROWS - 1; i++) { memcpy(ch[i], ch[i + 1], COLS); memcpy(at[i], at[i + 1], COLS); }
	clear_rows(ROWS - 1, ROWS - 1);
}

void ct8500::line_feed()
{
	if (r < ROWS - 1) r++;
	else roll_up();
	follow();
}

void ct8500::follow()
{
	// keep the cursor visible: the 25-line window follows it through the 50-line memory
	if (r < top) top = r;
	if (r > top + VIEW - 1) top = r - (VIEW - 1);
	if (top > ROWS - VIEW) top = ROWS - VIEW;
	if (top < 0) top = 0;
}

void ct8500::put(uint8_t v)
{
	ch[r][c] = v; at[r][c] = attr;
	if (++c >= COLS) { c = 0; line_feed(); }
}

void ct8500::feed(uint8_t v)
{
	v &= 0x7f;
	if (m_learn) {                        // remote learn: ESC 39, then ESC 4x, the string, US
		if (v == 0x1f) { m_learn = 0; return; }
		if (m_learn == 1) { if (v == 0x1b) m_learn = 2; return; }
		if (m_learn == 2) {
			m_learn_key = (v >= 0x48 && v <= 0x4f) ? v - 0x48 : -1;
			if (m_learn_key >= 0) m_fkey[m_learn_key].clear();
			m_learn = 3;
			return;
		}
		if (m_learn_key >= 0) { if (m_fkey[m_learn_key].size() < 64) m_fkey[m_learn_key] += char(v); else bell = true; }
		return;
	}
	if (esc == 1) {                       // command byte after ESC
		esc = 0;
		if (v == 0x7c || v == 0x70) { esc_cmd = 0x7c; esc = 2; return; }   // load cursor: 2 operands
		if (v == 0x3e) { esc_cmd = 0x3e; esc = 3; return; }                 // split screen: 1 operand
		command(v);
		return;
	}
	if (esc == 2) { op[0] = v; esc = 4; return; }
	if (esc == 4) {                       // ESC 7C line col
		esc = 0;
		int line = op[0] - 0x20, col = v - 0x20;
		if (line < 0) line = 0; if (line >= VIEW) line = VIEW - 1;
		if (col < 0) col = 0; if (col >= COLS) col = COLS - 1;
		r = top + line; c = col;
		return;
	}
	if (esc == 3) {                       // ESC 3E line
		esc = 0;
		split = v - 0x20; if (split < 0 || split >= ROWS) split = 0;
		return;
	}
	switch (v) {
	case 0x1b: esc = 1; return;
	case 0x00: case 0x7f: return;
	case 0x07: bell = true; return;       // bell: front ends take it
	case 0x08: if (c > 0) c--; return;
	case 0x09: { int i = c + 1; while (i < COLS && !tabs[i]) i++; c = i < COLS ? i : COLS - 1; return; }
	case 0x0a: line_feed(); return;
	case 0x0d: c = 0; return;
	default:
		if (v >= 0x20) put(v);
		return;
	}
}

void ct8500::command(uint8_t code)
{
	switch (code) {
	case 0x01: transmit(r, r, 0); break;                    // transmit line (up to ETX)
	case 0x02: transmit(r, top + VIEW - 1, c); break;       // transmit page: cursor to bottom (up to ETX)
	case 0x04: clear_rows(top, top + VIEW - 1); r = top; c = 0; break;   // clear screen
	case 0x05: clear_all(); break;                                        // clear all
	case 0x09: { int i = c - 1; while (i > 0 && !tabs[i]) i--; c = i > 0 ? i : 0; break; }  // tab back
	case 0x61: tabs[c] = true; break;
	case 0x62: tabs[c] = false; break;
	case 0x0c: {                          // insert line (within the displayed page)
		int end = top + VIEW - 1;
		for (int i = end; i > r; i--) { memcpy(ch[i], ch[i - 1], COLS); memcpy(at[i], at[i - 1], COLS); }
		clear_rows(r, r); c = 0;
		break;
	}
	case 0x0d: {                          // delete line
		int end = top + VIEW - 1;
		for (int i = r; i < end; i++) { memcpy(ch[i], ch[i + 1], COLS); memcpy(at[i], at[i + 1], COLS); }
		clear_rows(end, end); c = 0;
		break;
	}
	case 0x14: for (int i = c; i < COLS; i++) { ch[r][i] = ' '; at[r][i] = 0; } break;    // erase EOL
	case 0x15:                                                                          // erase EOP
		for (int i = c; i < COLS; i++) { ch[r][i] = ' '; at[r][i] = 0; }
		clear_rows(r + 1, top + VIEW - 1);
		break;
	case 0x1c:                            // insert character
		memmove(&ch[r][c + 1], &ch[r][c], COLS - c - 1); memmove(&at[r][c + 1], &at[r][c], COLS - c - 1);
		ch[r][c] = ' '; at[r][c] = 0;
		break;
	case 0x1d:                            // delete character
		memmove(&ch[r][c], &ch[r][c + 1], COLS - c - 1); memmove(&at[r][c], &at[r][c + 1], COLS - c - 1);
		ch[r][COLS - 1] = ' '; at[r][COLS - 1] = 0;
		break;
	case 0x1f: case 0x3a: m_learn = 0; m_learn_local = false; break;   // exit learn mode
	case 0x39: m_learn = 1; break;                // enter learn mode (remote): ESC 4x string US follows
	case 0x38: reply(m_learn || m_learn_local ? 0x31 : 0x30); break; // learn mode status (approximation)
	case 0x3b: case 0x3c: break;                  // monitor mode on/off: not modelled
	case 0x3d: m_mode = HOST; break;
	case 0x5d: m_mode = POWERUP; break;
	case 0x5f: m_mode = LOCAL; break;
	case 0x3f:                                    // screen alignment test
		for (int i = top; i < top + VIEW; i++) for (int j = 0; j < COLS; j++) { ch[i][j] = 'E'; at[i][j] = 0; }
		break;
	case 0x41: if (top > 0) top--; break;         // scroll down (window moves up in memory)
	case 0x42: if (top < ROWS - VIEW) top++; break;
	case 0x44: case 0x79: top = VIEW; r = top; c = 0; break;   // page down / page two
	case 0x45: case 0x59: top = 0; r = 0; c = 0; break;        // page up / page one
	case 0x50: if (c > 0) c--; break;
	case 0x51: if (r < ROWS - 1) r++; follow(); break;
	case 0x52: if (r > 0) r--; follow(); break;
	case 0x53: if (c < COLS - 1) c++; break;
	case 0x54: r = top; c = 0; break;
	case 0x5a: cursor_on = true; break;
	case 0x5b: cursor_on = false; break;
	case 0x7d: reply(uint8_t(0x20 + (r - top))); reply(uint8_t(0x20 + c)); break;
	case 0x69: reply(uint8_t(0x20 + top)); break;
	case 0x6a: kbd_locked = true; break;
	case 0x6b: kbd_locked = false; break;
	default:
		if (code >= 0x20 && code <= 0x2e) {       // display attributes
			uint8_t a = code - 0x20;
			attr = (a == 5) ? uint8_t(A_BLANK) : uint8_t(((a & 1) ? A_UL : 0) | ((a & 2) ? A_BLINK : 0) |
			                                          ((a & 4) ? A_INV : 0) | ((a & 8) ? A_DIM : 0));
		}
		break;                                    // unknown commands are ignored
	}
}

// SEND and remote 01/02: rows from_row..to_row, starting at from_col on the first,
// each followed by CR, stopping at the first ETX (03)
void ct8500::transmit(int from_row, int to_row, int from_col)
{
	for (int i = from_row; i <= to_row && i < ROWS; i++) {
		int e = COLS; while (e > 0 && ch[i][e - 1] == ' ') e--;
		for (int j = (i == from_row ? from_col : 0); j < e; j++) {
			if (ch[i][j] == 0x03) return;
			reply(ch[i][j]);
		}
		reply(0x0d);
	}
}

// a programmed function key: ESC sequences run as terminal commands (with their
// operands), other characters are typed
void ct8500::play(const std::string &str)
{
	for (size_t i = 0; i < str.size(); i++) {
		uint8_t b = uint8_t(str[i]);
		if (b == 0x1b) {
			feed(b);
			while (++i < str.size()) { feed(uint8_t(str[i])); if (!esc) break; }
		} else type(b);
	}
}

void ct8500::type(uint8_t v)
{
	if (kbd_locked) return;
	if (m_tty_lock && v >= 'a' && v <= 'z') v = uint8_t(v - 0x20);     // TTY LOCK: upper case only
	if (m_split_pending) { m_split_pending = 0; split = v - 0x20; if (split < 0 || split >= ROWS) split = 0; return; }
	if (m_learn_local) {                                                // recording a function key
		if (m_learn_key >= 0) { if (m_fkey[m_learn_key].size() < 64) m_fkey[m_learn_key] += char(v); else bell = true; }
		return;
	}
	if (m_mode == LOCAL) {                                              // local: displayed, not sent
		if (v == 0x1b) return;                                          // ESC has no local effect
		if (v == 0x7f) { put(0x7f); return; }                           // RUB OUT shows its glyph
		feed(v);
		return;
	}
	reply(v);
}

void ct8500::key(key_t k, bool shift, bool cntrl)
{
	if (kbd_locked) return;
	const bool fkey = k >= K_F1 && k <= K_F8;
	// CNTRL-SHIFT-Fn: the terminal's own commands (070-3737 §2)
	if (cntrl && shift && fkey) {
		switch (k) {
		case K_F1: command(0x38); break;                                 // learn status
		case K_F2: m_learn_local = true; m_learn_key = -1; break;         // enter learn mode
		case K_F3: m_learn_local = false; break;                         // exit learn mode
		case K_F4: case K_F5: break;                                     // monitor mode on/off: not modelled
		case K_F6: m_mode = HOST; break;                                 // host-control mode
		case K_F7: m_split_pending = 1; break;                           // split screen: next character is the line
		default: break;
		}
		return;
	}
	if (m_learn_local) {
		if (shift && fkey) { m_learn_key = k - K_F1; m_fkey[m_learn_key].clear(); }   // the key to program
		else if (m_learn_key >= 0 && k < 0x80) { type(0x1b); type(k); }              // a key's own code
		return;
	}
	switch (k) {
	case K_REMOTE: if (m_mode == LOCAL) m_mode = POWERUP; return;
	case K_LOCAL: m_mode = LOCAL; return;
	case K_TTY_LOCK: m_tty_lock = !m_tty_lock; return;
	case K_BREAK:
		if (shift || cntrl) {                                           // SEND: line or page, then on line
			if (shift) command(0x01); else command(0x02);
			if (m_mode == LOCAL) m_mode = POWERUP;
		} else if (m_mode != LOCAL && send_break) send_break();         // BREAK: 100 ms spacing
		return;
	case K_TAB:
		if (!shift) { type(0x09); return; }                              // TAB FWD is a typed HT
		break;
	default: break;
	}
	// the code this key stands for (shifted variants share a key)
	uint8_t code = uint8_t(k);
	if (shift) switch (k) {
		case K_INS_CHAR: code = 0x1d; break;      // DELETE CHAR
		case K_INS_LINE: code = 0x0d; break;      // DELETE LINE
		case K_ERASE_EOL: code = 0x15; break;     // ERASE TO EOP
		case K_CLEAR_SCREEN: code = 0x05; break;  // CLEAR ALL
		case K_TAB_SET: code = 0x62; break;       // TAB CLEAR
		case K_TAB: code = 0x09; break;           // TAB BACK
		default: break;
	}
	if (k == K_HOME && cntrl) code = shift ? 0x79 : 0x59;   // home of Page Two / Page One
	if (fkey) {
		if (m_mode == HOST) { reply_esc(code); return; }
		if (shift) {                                        // programmable: play the string, else send the code
			const std::string &str = m_fkey[k - K_F1];
			if (!str.empty()) play(str); else reply_esc(code);
			return;
		}
		switch (k) {                                        // unshifted, power-up and local
		case K_F2: command(0x41); return;                   // SCROLL: data down
		case K_F3: command(0x42); return;                   // SCROLL: data up
		case K_F5: command(0x45); return;                   // PAGE: Page One
		case K_F6: command(0x44); return;                   // PAGE: Page Two
		default: reply_esc(code); return;                   // F1/F4/F7/F8: for Tektronix editors
		}
	}
	if (m_mode == HOST) { reply_esc(code); return; }        // host-control: keys only send codes
	// power-up and local: act locally, within the displayed 25 lines (070-3737 1-18)
	const int bottom = top + VIEW - 1;
	switch (code) {
	case 0x52: r = r > top ? r - 1 : bottom; return;        // up; from the top line, to the bottom line
	case 0x51: r = r < bottom ? r + 1 : top; return;        // down; from the bottom line, to the top line
	case 0x50:                                              // left; wraps to the previous line's end
		if (c > 0) c--;
		else { c = COLS - 1; r = r > top ? r - 1 : bottom; }
		return;
	case 0x53:                                              // right; wraps to the next line, scrolling at the end
		if (c < COLS - 1) c++;
		else { c = 0; if (r < bottom) r++; else line_feed(); }
		return;
	case 0x54: r = top; c = 0; return;                      // home
	default: command(code); return;                         // editing, erase, clear, tabs, page homes
	}
}

std::string ct8500::text() const
{
	std::string s;
	for (int i = 0; i < VIEW; i++) {
		int e = COLS; while (e > 0 && ch[top + i][e - 1] == ' ') e--;
		for (int j = 0; j < e; j++) s += (at[top + i][j] & A_BLANK) ? ' ' : char(ch[top + i][j]);
		s += '\n';
	}
	char buf[64]; snprintf(buf, sizeof buf, "[cursor %d,%d top %d mode %s]\n", r - top, c, top,
		m_mode == HOST ? "host" : m_mode == LOCAL ? "local" : "power-up");
	return s + buf;
}

std::string ct8500::state() const
{
	char buf[64];
	snprintf(buf, sizeof buf, "CT8500 %d %d %d %d %d\n", COLS, VIEW, r - top, c, cursor_on ? 1 : 0);
	std::string s = buf;
	for (int i = 0; i < VIEW; i++) {
		for (int j = 0; j < COLS; j++) { uint8_t v = ch[top + i][j]; s += (v >= 0x20 && v < 0x7f && !(at[top + i][j] & A_BLANK)) ? char(v) : ' '; }
		s += '\n';
	}
	static const char hx[] = "0123456789abcdef";
	for (int i = 0; i < VIEW; i++) {
		for (int j = 0; j < COLS; j++) s += hx[at[top + i][j] & 0x0f];
		s += '\n';
	}
	return s;
}
