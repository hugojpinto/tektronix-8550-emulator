// Host terminal helpers (POSIX): raw mode and non-blocking key polling.
#ifndef TEK_HOST_TTY_H
#define TEK_HOST_TTY_H
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

namespace host {
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
