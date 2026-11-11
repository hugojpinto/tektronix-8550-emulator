// Tektronix CT8500 Video Display Terminal -- behavioural model. Front ends draw it:
// host/ct8500_ansi.* (a host terminal, ANSI) and host/ct8500_raster.* (dots, for the GUI).
// Source: CT8500 Operators Manual 070-3737-00 (remote commands, Table 2-4 addressing).
//
// Memory: 50 lines x 80 columns (Page One = lines 0-24, Page Two = 25-49);
// the display shows 25 lines starting at 'top'. Remote commands are ESC + code
// (+ operands for cursor load / split screen). In host-control mode (ESC 3D) the
// keyboard sends ESC + function code instead of acting locally.
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

	// the keyboard: printable characters and control codes as typed, and the
	// CT8500's special keys (cursor keys act locally unless the host took control)
	enum key_t : uint8_t {
		K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52, K_RIGHT = 0x53, K_HOME = 0x54,
		K_F1 = 0x48, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8,   // F5 = PAGE DOWN, F6 = PAGE UP
		K_INS_CHAR = 0x1c, K_DEL_CHAR = 0x1d, K_INS_LINE = 0x0c, K_DEL_LINE = 0x0d,
		K_ERASE_EOL = 0x14, K_ERASE_EOP = 0x15, K_BACK_TAB = 0x09,
	};
	void type(uint8_t ch) { if (!kbd_locked) reply(ch); }
	void key(key_t k);

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
	bool cursor_on = true, kbd_locked = false, learn = false, bell = false;
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
	int page_end() const { return top < VIEW ? VIEW - 1 : ROWS - 1; }
};
#endif
