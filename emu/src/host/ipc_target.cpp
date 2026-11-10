// Prototype over IPC: synchronous 4-byte request / 2-byte response protocol.
#include "host/ipc_target.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

bool ipc_target::open(const std::string &spec, std::string &err)
{
	if (spec.rfind("unix:", 0) == 0) {
		sockaddr_un sa{}; sa.sun_family = AF_UNIX;
		std::string path = spec.substr(5);
		if (path.size() >= sizeof sa.sun_path) { err = "socket path too long"; return false; }
		strcpy(sa.sun_path, path.c_str());
		m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (m_fd < 0 || connect(m_fd, (sockaddr *)&sa, sizeof sa) != 0) { err = "cannot connect to " + spec; if (m_fd >= 0) close(m_fd); m_fd = -1; return false; }
	} else if (spec.rfind("tcp:", 0) == 0) {
		std::string hp = spec.substr(4); size_t c = hp.rfind(':');
		if (c == std::string::npos) { err = "tcp spec must be tcp:host:port"; return false; }
		addrinfo hints{}, *res = nullptr; hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(hp.substr(0, c).c_str(), hp.substr(c + 1).c_str(), &hints, &res) != 0 || !res) { err = "cannot resolve " + spec; return false; }
		m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		bool ok = m_fd >= 0 && connect(m_fd, res->ai_addr, res->ai_addrlen) == 0;
		freeaddrinfo(res);
		if (!ok) { err = "cannot connect to " + spec; if (m_fd >= 0) close(m_fd); m_fd = -1; return false; }
		int one = 1; setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	} else { err = "target spec must be unix:PATH or tcp:HOST:PORT"; return false; }
	// hello: offer our highest protocol version; the target answers the version it
	// will speak (<= ours). v1 targets always answer 1. v2 adds 'F' (opcode fetch).
	u8 ver = req('H', 0x0002, 0);
	if (m_fd < 0) { err = "target closed the connection"; return false; }
	if (ver < 1 || ver > 2) { err = "target speaks protocol version " + std::to_string(ver) + ", expected 1 or 2"; close(m_fd); m_fd = -1; return false; }
	m_version = ver;
	fprintf(stderr, "[prototype link: protocol v%d]\r\n", ver);
	return true;
}

ipc_target::~ipc_target() { if (m_fd >= 0) close(m_fd); }

u8 ipc_target::req(char op, u16 a, u8 d)
{
	if (m_fd < 0) return 0xff;
	u8 q[4] = { u8(op), u8(a), u8(a >> 8), d }, r[2];
	size_t got = 0;
	if (write(m_fd, q, 4) != 4) goto fail;
	while (got < 2) { ssize_t n = read(m_fd, r + got, 2 - got); if (n <= 0) goto fail; got += size_t(n); }
	m_lines = r[1];
	m_requests++;
	return r[0];
fail:
	fprintf(stderr, "[prototype link lost]\r\n");
	close(m_fd); m_fd = -1; m_lines = 0;
	return 0xff;
}

void ipc_target::poll(unsigned long cyc)
{
	// time advance about every 256 instructions, in microseconds of real elapsed
	// Z80 time (T-states / 4 MHz); the fraction carries over so no time is lost.
	// The first call after a pause (cycle counter jumped back or reset) only resyncs.
	if (!m_cyc_valid || cyc < m_cyc_last) { m_cyc_valid = true; m_cyc_last = cyc; return; }
	if (++m_poll < 256) return;
	m_poll = 0;
	m_us_frac += double(cyc - m_cyc_last) * 1e6 / Z80_HZ;
	m_cyc_last = cyc;
	while (m_us_frac >= 1.0) {
		unsigned us = m_us_frac > 65535.0 ? 65535u : unsigned(m_us_frac);
		req('T', u16(us), 0);
		m_us_frac -= us;
	}
}
