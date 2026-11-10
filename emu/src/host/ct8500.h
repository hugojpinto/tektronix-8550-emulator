// Tektronix CT8500 Video Display Terminal -- behavioural model + ANSI renderer.
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

	// host keyboard input: a key or ANSI escape sequence from the user's terminal
	void key(const std::string &seq);

	// ANSI output that brings the user's terminal up to date
	std::string render(bool full = false);
	std::string enter_screen() const { return "\x1b[?1049h\x1b[2J"; }
	std::string leave_screen() const { return "\x1b[0m\x1b[?25h\x1b[?1049l"; }

	// plain-text dump of the visible page (testing)
	std::string text() const;
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
	// renderer shadow
	uint8_t sh_ch[VIEW][COLS], sh_at[VIEW][COLS];
	bool sh_valid = false; int sh_top = -1, sh_r = -1, sh_c = -1; bool sh_cur = true;

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
