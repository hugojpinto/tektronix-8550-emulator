// Non-blocking socket line for the 8301 REMOTE port.
#include "host/serial_link.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

bool serial_link::open(const std::string &spec, std::string &err)
{
	m_spec = spec;
	if (spec.rfind("listen:", 0) == 0) {
		int port = atoi(spec.c_str() + 7);
		m_listen = net::listen_local(port, err);
		if (m_listen == net::BAD) return false;
		fprintf(stderr, "[remote port: listening on 127.0.0.1:%d]\r\n", port);
		return true;
	}
	if (spec.rfind("unix:", 0) != 0 && spec.rfind("tcp:", 0) != 0) { err = "remote spec must be tcp:HOST:PORT, listen:PORT or unix:PATH"; return false; }
	m_fd = net::connect_spec(spec, err);
	if (m_fd == net::BAD) return false;
	net::set_nonblock(m_fd);
	fprintf(stderr, "[remote port: connected to %s]\r\n", spec.c_str());
	return true;
}

serial_link::~serial_link()
{
	if (m_fd != net::BAD) { flush(); net::close(m_fd); }
	net::close(m_listen);
}

void serial_link::accept_pending()
{
	if (m_fd != net::BAD || m_listen == net::BAD) return;
	net::sock fd = net::accept_nb(m_listen);
	if (fd == net::BAD) return;
	m_fd = fd;
	m_out.clear();                // what was sent while nobody listened is lost, like a cable not plugged in
	fprintf(stderr, "[remote port: peer connected]\r\n");
}

void serial_link::send(u8 c)
{
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
	fprintf(stderr, "[remote port: %s]\r\n", why);
	net::close(m_fd); m_fd = net::BAD; m_out.clear();
}
