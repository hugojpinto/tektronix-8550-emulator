// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// What is plugged into one of the lab's RS-232 ports: the 8301 REMOTE port
// (J102: DOS/50 COMM and HOST, notes/host-link.md), the 8301 AUX port, and the
// 8501 AUXILIARY and PRINTER ports. Plain bytes in both directions, as on the cable.
//   tcp:HOST:PORT    connect to a host program
//   listen:PORT      accept one connection on 127.0.0.1:PORT (re-accepts after a disconnect)
//   unix:PATH        connect to a Unix socket
//   file:PATH        a printer: what the port sends is appended to PATH; nothing comes back
//   loop             a wrap-back connector: what the port sends comes back to it
// Never blocks: the machine keeps running while nothing is connected.
#ifndef TEK_HOST_SERIAL_LINK_H
#define TEK_HOST_SERIAL_LINK_H
#include "emu.h"
#include "host/net.h"
#include <string>
#include <deque>
#include <cstdio>

class serial_link
{
public:
	~serial_link();
	// name: how messages call the port ("remote port", "8501 printer port", ...)
	bool open(const std::string &spec, std::string &err, const std::string &name = "remote port");
	bool active() const { return m_listen != net::BAD || m_fd != net::BAD || m_file || m_loop; }
	bool loopback() const { return m_loop; }
	bool connected() const { return m_fd != net::BAD || m_file || m_loop; }
	void send(u8 c);                       // queued; flushed by poll()
	template <typename F> void poll(F rx)  // deliver received bytes to rx(u8)
	{
		if (m_loop) { while (!m_back.empty()) { u8 c = m_back.front(); m_back.pop_front(); m_rx++; rx(c); } return; }
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
	std::string m_spec, m_name;
	std::deque<u8> m_out, m_back;
	FILE *m_file = nullptr;
	bool m_loop = false;
	unsigned long m_tx = 0, m_rx = 0;
	void accept_pending();
	void flush();
	long read_some(u8 *buf, size_t n);
	void drop(const char *why);
};
#endif
