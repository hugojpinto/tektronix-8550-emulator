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
	if (learn) {                          // programming a function key: swallow until US
		if (v == 0x1f) learn = false;
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
	case 0x07: bell = true; return;       // bell: emitted by render()
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
	case 0x01: {                          // transmit line
		int e = COLS; while (e > 0 && ch[r][e - 1] == ' ') e--;
		for (int i = 0; i < e; i++) reply(ch[r][i]);
		reply(0x0d);
		break;
	}
	case 0x02:                            // transmit page
		for (int i = top; i < top + VIEW; i++) {
			int e = COLS; while (e > 0 && ch[i][e - 1] == ' ') e--;
			for (int j = 0; j < e; j++) reply(ch[i][j]);
			reply(0x0d);
		}
		break;
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
	case 0x1f: case 0x3a: learn = false; break;   // exit learn mode
	case 0x39: learn = true; break;               // enter learn mode
	case 0x38: reply(learn ? 0x31 : 0x30); break; // learn mode status (approximation)
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

void ct8500::key(const std::string &k)
{
	if (kbd_locked || k.empty()) return;
	auto esc_code = [&](uint8_t code) { reply_esc(code); };
	// map the user's terminal keys to CT8500 keys
	struct map_t { const char *seq; uint8_t code; bool cursor; } static const m[] = {
		{ "\x1b[A", 0x52, true }, { "\x1b[B", 0x51, true }, { "\x1b[C", 0x53, true }, { "\x1b[D", 0x50, true },
		{ "\x1bOA", 0x52, true }, { "\x1bOB", 0x51, true }, { "\x1bOC", 0x53, true }, { "\x1bOD", 0x50, true },
		{ "\x1b[H", 0x54, true }, { "\x1bOH", 0x54, true }, { "\x1b[1~", 0x54, true },
		{ "\x1bOP", 0x48 }, { "\x1bOQ", 0x49 }, { "\x1bOR", 0x4a }, { "\x1bOS", 0x4b },      // F1-F4
		{ "\x1b[11~", 0x48 }, { "\x1b[12~", 0x49 }, { "\x1b[13~", 0x4a }, { "\x1b[14~", 0x4b },
		{ "\x1b[15~", 0x4c }, { "\x1b[17~", 0x4d }, { "\x1b[18~", 0x4e }, { "\x1b[19~", 0x4f }, // F5-F8
		{ "\x1b[6~", 0x4c }, { "\x1b[5~", 0x4d },              // PgDn = F5 (PAGE DOWN), PgUp = F6 (PAGE UP)
		{ "\x1b[2~", 0x1c }, { "\x1b[3~", 0x1d },              // Insert/Delete = INSERT/DELETE CHAR
		{ "\x1b[20~", 0x0c }, { "\x1b[21~", 0x0d },            // F9/F10 = INSERT/DELETE LINE
		{ "\x1b[23~", 0x14 }, { "\x1b[24~", 0x15 },            // F11/F12 = ERASE EOL/EOP
		{ "\x1b[Z", 0x09 },                                    // Shift-Tab = BACK TAB
	};
	for (const auto &e : m)
		if (k == e.seq) {
			if (m_mode != HOST && e.cursor) {               // power-up/local: cursor keys act locally
				switch (e.code) {
				case 0x50: if (c > 0) c--; break;
				case 0x51: if (r < ROWS - 1) r++; follow(); break;
				case 0x52: if (r > 0) r--; follow(); break;
				case 0x53: if (c < COLS - 1) c++; break;
				case 0x54: r = top; c = 0; break;
				}
				return;
			}
			esc_code(e.code);
			return;
		}
	if (k[0] == 0x1b && k.size() > 1) return;             // unknown escape sequence: drop
	for (char ch1 : k) reply(uint8_t(ch1 == '\n' ? '\r' : ch1));
}

std::string ct8500::render(bool full)
{
	std::string o;
	if (!sh_valid || full) { o += "\x1b[0m\x1b[2J"; memset(sh_ch, 0, sizeof sh_ch); memset(sh_at, 0xff, sizeof sh_at); sh_valid = true; }
	uint8_t cur = 0xff;
	auto sgr = [&](uint8_t a) {
		if (a == cur) return;
		o += "\x1b[0";
		if (a & A_UL) o += ";4";
		if (a & A_BLINK) o += ";5";
		if (a & A_INV) o += ";7";
		if (a & A_DIM) o += ";2";
		o += "m"; cur = a;
	};
	bool moved = false;
	for (int i = 0; i < VIEW; i++) {
		const uint8_t *rc = ch[top + i], *ra = at[top + i];
		if (!memcmp(rc, sh_ch[i], COLS) && !memcmp(ra, sh_at[i], COLS)) continue;
		// repaint from the first differing column to the end of the row
		int f = 0; while (f < COLS && rc[f] == sh_ch[i][f] && ra[f] == sh_at[i][f]) f++;
		char buf[24]; snprintf(buf, sizeof buf, "\x1b[%d;%dH", i + 1, f + 1); o += buf;
		for (int j = f; j < COLS; j++) {
			sgr(ra[j] & ~A_BLANK);
			o += (ra[j] & A_BLANK) ? ' ' : char(rc[j] >= 0x20 && rc[j] < 0x7f ? rc[j] : ' ');
		}
		memcpy(sh_ch[i], rc, COLS); memcpy(sh_at[i], ra, COLS);
		moved = true;
	}
	if (cur != 0xff) o += "\x1b[0m";
	if (moved || r != sh_r || c != sh_c || top != sh_top) {
		char buf[24]; snprintf(buf, sizeof buf, "\x1b[%d;%dH", r - top + 1, c + 1); o += buf;
		sh_r = r; sh_c = c; sh_top = top;
	}
	if (bell) { o += '\a'; bell = false; }
	if (cursor_on != sh_cur) { o += cursor_on ? "\x1b[?25h" : "\x1b[?25l"; sh_cur = cursor_on; }
	return o;
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
