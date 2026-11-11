// A serial line to the outside world over a socket: the 8301's REMOTE port
// (J102: DOS/50 COMM and HOST commands). Plain bytes in both directions,
// as on the RS-232 cable; see notes/host-link.md.
//   tcp:HOST:PORT    connect to a host program
//   listen:PORT      accept one connection on 127.0.0.1:PORT (re-accepts after a disconnect)
//   unix:PATH        connect to a Unix socket
// Never blocks: the machine keeps running while nothing is connected.
#ifndef TEK_HOST_SERIAL_LINK_H
#define TEK_HOST_SERIAL_LINK_H
#include "emu.h"
#include "host/net.h"
#include <string>
#include <deque>

class serial_link
{
public:
	~serial_link();
	bool open(const std::string &spec, std::string &err);
	bool active() const { return m_listen != net::BAD || m_fd != net::BAD; }
	bool connected() const { return m_fd != net::BAD; }
	void send(u8 c);                       // queued; flushed by poll()
	template <typename F> void poll(F rx)  // deliver received bytes to rx(u8)
	{
		accept_pending();
		if (m_fd == net::BAD) return;
		flush();
		u8 buf[256];
		for (;;) {
			long n = read_some(buf, sizeof buf);
			if (n <= 0) break;
			for (long i = 0; i < n; i++) rx(buf[i]);
		}
	}
	unsigned long tx_count() const { return m_tx; }
	unsigned long rx_count() const { return m_rx; }

private:
	net::sock m_fd = net::BAD, m_listen = net::BAD;
	std::string m_spec;
	std::deque<u8> m_out;
	unsigned long m_tx = 0, m_rx = 0;
	void accept_pending();
	void flush();
	long read_some(u8 *buf, size_t n);
	void drop(const char *why);
};
#endif
