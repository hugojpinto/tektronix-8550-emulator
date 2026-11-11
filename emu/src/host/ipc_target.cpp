// Prototype over IPC: synchronous 4-byte request / 2-byte response protocol.
#include "host/ipc_target.h"
#include <cstring>
#include <cstdio>

bool ipc_target::open(const std::string &spec, std::string &err)
{
	if (spec.rfind("unix:", 0) != 0 && spec.rfind("tcp:", 0) != 0) { err = "target spec must be unix:PATH or tcp:HOST:PORT"; return false; }
	m_fd = net::connect_spec(spec, err);
	if (m_fd == net::BAD) return false;
	// hello: offer our highest protocol version; the target answers the version it
	// will speak (<= ours). v1 targets always answer 1. v2 adds 'F' (opcode fetch);
	// v3 adds WAIT / RESET in the reply lines and target power in the H reply.
	u8 ver = req('H', 0x0003, 0);
	if (m_fd == net::BAD) { err = "target closed the connection"; return false; }
	if (ver < 1 || ver > 3) { err = "target speaks protocol version " + std::to_string(ver) + ", expected 1 to 3"; net::close(m_fd); m_fd = net::BAD; return false; }
	m_version = ver;
	m_power = ver < 3 || (m_lines & 8);
	m_lines &= 3;
	m_wait = 0;
	fprintf(stderr, "[prototype link: protocol v%d%s]\r\n", ver, m_power ? "" : ", NO TARGET POWER");
	return true;
}

ipc_target::~ipc_target() { net::close(m_fd); }

u8 ipc_target::req(char op, u16 a, u8 d)
{
	if (m_fd == net::BAD) return 0xff;
	u8 q[4] = { u8(op), u8(a), u8(a >> 8), d }, r[2];
	size_t got = 0;
	if (net::send_some(m_fd, q, 4) != 4) goto fail;
	while (got < 2) { long n = net::recv_some(m_fd, r + got, 2 - got); if (n <= 0) goto fail; got += size_t(n); }
	// lines: bit 0 INT, 1 NMI; v3: bit 2 RESET, bits 7-4 WAIT T-states of this cycle
	m_lines = m_version >= 3 || op == 'H' ? r[1] : u8(r[1] & 3);
	if (m_version >= 3 && op != 'H') m_wait += r[1] >> 4;
	m_requests++;
	return r[0];
fail:
	fprintf(stderr, "[prototype link lost]\r\n");
	net::close(m_fd); m_fd = net::BAD; m_lines = 0;
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
