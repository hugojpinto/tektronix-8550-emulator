// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Tektronix CT8500 Video Display Terminal -- behavioural model. Front ends draw it:
// host/ct8500_ansi.* (a host terminal, ANSI) and host/ct8500_raster.* (dots, for the GUI).
// Source: CT8500 Operators Manual 070-3737-00 (remote commands, Table 2-4 addressing).
//
// Memory: 50 lines x 80 columns (Page One = lines 0-24, Page Two = 25-49);
// the display shows 25 lines starting at 'top'. Remote commands are ESC + code
// (+ operands for cursor load / split screen).
#ifndef TEK_HOST_CT8500_H
#define TEK_HOST_CT8500_H
#include <cstdint>
#include <string>
#include <functional>
#include <vector>

class ct8500
{
public:
	static constexpr int ROWS = 50, COLS = 80, VIEW = 25;
	enum : uint8_t { A_UL = 1, A_BLINK = 2, A_INV = 4, A_DIM = 8, A_BLANK = 16 };
	enum mode_t { POWERUP, HOST, LOCAL };

	ct8500() { clear_all(); for (int c = 1; c < COLS; c += 8) tabs[c] = true; }

	// bytes from the computer (8301 system terminal port)
	void feed(uint8_t c);
	// bytes the terminal sends to the computer (replies and keystrokes)
	std::function<void(uint8_t)> send;

	// The keyboard (070-3737 §1 "The Terminal Keyboard", §2 procedures). In power-up mode
	// typed characters go to the host and the editing/paging/cursor keys act locally;
	// in local mode everything acts locally (text reaches the host only through SEND);
	// in host-control mode every key sends ESC + its function code instead.
	// Physical keys; each value is the function code the key sends in host-control mode.
	enum key_t : uint8_t {
		K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52, K_RIGHT = 0x53, K_HOME = 0x54,
		K_F1 = 0x48, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8,   // F2/F3 SCROLL, F5/F6 PAGE
		K_INS_CHAR = 0x1c,       // shifted: DELETE CHAR (1D)
		K_INS_LINE = 0x0c,       // shifted: DELETE LINE (0D)
		K_ERASE_EOL = 0x14,      // shifted: ERASE TO EOP (15)
		K_CLEAR_SCREEN = 0x04,   // shifted: CLEAR ALL (05)
		K_TAB_SET = 0x61,        // shifted: TAB CLEAR (62)
		K_TAB = 0x09,            // TAB FWD (types 09); shifted: TAB BACK
		K_BREAK = 0x80,          // shifted: SEND (line); CNTRL: SEND (page)
		K_REMOTE, K_LOCAL, K_TTY_LOCK,
	};
	// a printable character or control code as typed (CNTRL-letter gives 01-1A)
	void type(uint8_t ch);
	void key(key_t k, bool shift = false, bool cntrl = false);
	// BREAK: the line goes to the spacing level for 100 ms (power-up and host-control modes)
	std::function<void()> send_break;
	bool tty_lock() const { return m_tty_lock; }
	bool learning() const { return m_learn_local; }
	// the string programmed into shifted function key n (0-7), as stored by learn mode
	const std::string &fkey_string(int n) const { return m_fkey[n & 7]; }

	// what is on the screen (row 0..VIEW-1 of the displayed window)
	uint8_t cell(int row, int col) const { return ch[top + row][col]; }
	uint8_t attrs(int row, int col) const { return at[top + row][col]; }
	int cursor_row() const { return r - top; }
	int cursor_col() const { return c; }
	bool cursor_visible() const { return cursor_on; }
	int top_line() const { return top; }
	bool take_bell() { bool b = bell; bell = false; return b; }

	// plain-text dump of the visible page (testing)
	std::string text() const;
	// full visible state for screenshots: header line, 25 text rows, 25 attribute rows (hex)
	std::string state() const;
	mode_t mode() const { return m_mode; }

private:
	uint8_t ch[ROWS][COLS], at[ROWS][COLS];
	bool tabs[COLS] = {};
	int r = 0, c = 0, top = 0, split = 0;
	uint8_t attr = 0;
	bool cursor_on = true, kbd_locked = false, bell = false;
	// learn mode: programming the shifted function keys (remote: ESC 39 ESC 4x string US;
	// local: CNTRL-SHIFT-F2, SHIFT-Fn, the string, CNTRL-SHIFT-F3). Up to 64 characters.
	std::string m_fkey[8];
	int m_learn = 0;             // remote learn: 0 off, 1 expect ESC, 2 expect key code, 3 collecting
	int m_learn_key = -1;
	bool m_learn_local = false;  // local learn: keys typed are recorded
	bool m_tty_lock = false;
	int m_split_pending = 0;     // CNTRL-SHIFT-F7: the next typed character is the split line
	mode_t m_mode = POWERUP;
	int esc = 0;                 // 0 none, 1 after ESC, 2+ collecting operands
	uint8_t esc_cmd = 0, op[2] = {};

	void clear_all();
	void clear_rows(int from, int to);
	void put(uint8_t v);
	void line_feed();
	void roll_up();
	void command(uint8_t code);
	void follow();
	void reply(uint8_t v) { if (send) send(v); }
	void reply_esc(uint8_t code) { reply(0x1b); reply(code); }
	void transmit(int from_row, int to_row, int from_col);   // SEND / remote 01, 02: up to ETX
	void play(const std::string &s);                         // a programmed function key
	int page_end() const { return top < VIEW ? VIEW - 1 : ROWS - 1; }
};
#endif
