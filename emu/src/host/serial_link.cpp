// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Non-blocking lines for the lab's RS-232 ports.
#include "host/serial_link.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

bool serial_link::open(const std::string &spec, std::string &err, const std::string &name)
{
	m_spec = spec; m_name = name;
	const char *nm = m_name.c_str();
	if (spec == "loop") { m_loop = true; return true; }
	if (spec.rfind("file:", 0) == 0) {
		m_file = fopen(spec.c_str() + 5, "ab");
		if (!m_file) { err = "cannot open " + spec.substr(5); return false; }
		return true;
	}
	if (spec.rfind("listen:", 0) == 0) {
		int port = atoi(spec.c_str() + 7);
		m_listen = net::listen_local(port, err);
		if (m_listen == net::BAD) return false;
		fprintf(stderr, "[%s: listening on 127.0.0.1:%d]\r\n", nm, port);
		return true;
	}
	if (spec.rfind("unix:", 0) != 0 && spec.rfind("tcp:", 0) != 0) { err = name + ": the spec must be tcp:HOST:PORT, listen:PORT, unix:PATH, file:PATH or loop"; return false; }
	m_fd = net::connect_spec(spec, err);
	if (m_fd == net::BAD) return false;
	net::set_nonblock(m_fd);
	fprintf(stderr, "[%s: connected to %s]\r\n", nm, spec.c_str());
	return true;
}

serial_link::~serial_link()
{
	if (m_fd != net::BAD) { flush(); net::close(m_fd); }
	if (m_file) fclose(m_file);
	net::close(m_listen);
}

void serial_link::accept_pending()
{
	if (m_fd != net::BAD || m_listen == net::BAD) return;
	net::sock fd = net::accept_nb(m_listen);
	if (fd == net::BAD) return;
	m_fd = fd;
	m_out.clear();                // what was sent while nobody listened is lost, like a cable not plugged in
	fprintf(stderr, "[%s: peer connected]\r\n", m_name.c_str());
}

void serial_link::send(u8 c)
{
	if (m_loop) { m_back.push_back(c); m_tx++; return; }
	if (m_file) { fputc(c, m_file); fflush(m_file); m_tx++; return; }
	if (m_fd == net::BAD) return; // no cable: the byte goes nowhere
	m_out.push_back(c);
	m_tx++;
	if (m_out.size() >= 64) flush();
}

void serial_link::flush()
{
	while (m_fd != net::BAD && !m_out.empty()) {
		u8 buf[512]; size_t n = 0;
		while (n < sizeof buf && n < m_out.size()) { buf[n] = m_out[n]; n++; }
		long w = net::send_some(m_fd, buf, n);
		if (w > 0) { m_out.erase(m_out.begin(), m_out.begin() + w); continue; }
		if (w < 0 && net::would_block()) return;
		drop("write failed");
	}
}

long serial_link::read_some(u8 *buf, size_t n)
{
	long r = net::recv_some(m_fd, buf, n);
	if (r > 0) { m_rx += size_t(r); return r; }
	if (r == 0) { drop("peer closed"); return 0; }
	if (!net::would_block()) drop("read failed");
	return 0;
}

void serial_link::drop(const char *why)
{
	fprintf(stderr, "[%s: %s]\r\n", m_name.c_str(), why);
	net::close(m_fd); m_fd = net::BAD; m_out.clear();
}
