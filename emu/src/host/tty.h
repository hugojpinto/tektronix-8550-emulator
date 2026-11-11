// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Host terminal helpers: raw mode, non-blocking key polling, isatty.
// POSIX: termios + select. Windows: the console in VT mode (ANSI output, as the
// CT8500 renderer emits it) and _kbhit/_getch, with special keys translated into
// the escape sequences a Unix terminal would send, so one key parser serves both.
#ifndef TEK_HOST_TTY_H
#define TEK_HOST_TTY_H
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <cstdio>

namespace host {
inline bool is_tty(int fd) { return _isatty(fd) != 0; }
inline void tty_raw(bool on)
{
	static DWORD in0 = 0, out0 = 0; static bool raw = false;
	HANDLE hi = GetStdHandle(STD_INPUT_HANDLE), ho = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!_isatty(0)) return;
	if (on && !raw) {
		GetConsoleMode(hi, &in0); GetConsoleMode(ho, &out0);
		SetConsoleMode(hi, in0 & ~DWORD(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
		SetConsoleMode(ho, out0 | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
		SetConsoleOutputCP(CP_UTF8);
		raw = true;
	} else if (!on && raw) { SetConsoleMode(hi, in0); SetConsoleMode(ho, out0); raw = false; }
}
// >= 0: a key byte; -1: nothing pending; -2: EOF (never on a console)
inline int poll_key()
{
	static std::string pending;
	if (!pending.empty()) { int c = (unsigned char)pending[0]; pending.erase(0, 1); return c; }
	if (!_isatty(0) || !_kbhit()) return -1;
	int c = _getch();
	if (c != 0 && c != 0xe0) return c == '\r' ? '\r' : c;
	// extended key: translate to the VT sequence a Unix terminal sends
	int k = _getch();
	const char *seq = nullptr;
	switch (k) {
	case 72: seq = "\x1b[A"; break;  case 80: seq = "\x1b[B"; break;      // up, down
	case 77: seq = "\x1b[C"; break;  case 75: seq = "\x1b[D"; break;      // right, left
	case 71: seq = "\x1b[H"; break;                                         // home
	case 82: seq = "\x1b[2~"; break; case 83: seq = "\x1b[3~"; break;     // insert, delete
	case 73: seq = "\x1b[5~"; break; case 81: seq = "\x1b[6~"; break;     // page up, page down
	case 59: seq = "\x1bOP"; break;  case 60: seq = "\x1bOQ"; break;      // F1, F2
	case 61: seq = "\x1bOR"; break;  case 62: seq = "\x1bOS"; break;      // F3, F4
	case 63: seq = "\x1b[15~"; break; case 64: seq = "\x1b[17~"; break;   // F5, F6
	case 65: seq = "\x1b[18~"; break; case 66: seq = "\x1b[19~"; break;   // F7, F8
	case 67: seq = "\x1b[20~"; break; case 68: seq = "\x1b[21~"; break;   // F9, F10
	case 133: seq = "\x1b[23~"; break; case 134: seq = "\x1b[24~"; break; // F11, F12
	default: return -1;
	}
	pending = seq;
	int first = (unsigned char)pending[0]; pending.erase(0, 1);
	return first;
}
}

#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

namespace host {
inline bool is_tty(int fd) { return isatty(fd) != 0; }
inline termios &saved_tty() { static termios t; return t; }
inline bool &raw_flag() { static bool r = false; return r; }
inline void tty_raw(bool on)
{
	if (!isatty(0)) return;
	if (on && !raw_flag()) {
		tcgetattr(0, &saved_tty());
		termios t = saved_tty(); cfmakeraw(&t); t.c_oflag |= OPOST | ONLCR;
		tcsetattr(0, TCSANOW, &t); raw_flag() = true;
	} else if (!on && raw_flag()) { tcsetattr(0, TCSANOW, &saved_tty()); raw_flag() = false; }
}
// >= 0: a key; -1: nothing pending; -2: EOF
inline int poll_key()
{
	fd_set fs; FD_ZERO(&fs); FD_SET(0, &fs); timeval tv{0, 0};
	if (select(1, &fs, nullptr, nullptr, &tv) > 0) { unsigned char c; return read(0, &c, 1) == 1 ? c : -2; }
	return -1;
}
}
#endif
#endif
