// Non-blocking socket line for the 8301 REMOTE port.
#include "host/serial_link.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

bool serial_link::open(const std::string &spec, std::string &err)
{
	m_spec = spec;
	if (spec.rfind("listen:", 0) == 0) {
		int port = atoi(spec.c_str() + 7);
		m_listen = socket(AF_INET, SOCK_STREAM, 0);
		int one = 1; setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(u16(port)); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (m_listen < 0 || bind(m_listen, (sockaddr *)&sa, sizeof sa) != 0 || listen(m_listen, 1) != 0) {
			err = "cannot listen on 127.0.0.1:" + std::to_string(port); if (m_listen >= 0) close(m_listen); m_listen = -1; return false;
		}
		nonblock(m_listen);
		fprintf(stderr, "[remote port: listening on 127.0.0.1:%d]\r\n", port);
		return true;
	}
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
	} else { err = "remote spec must be tcp:HOST:PORT, listen:PORT or unix:PATH"; return false; }
	nonblock(m_fd);
	fprintf(stderr, "[remote port: connected to %s]\r\n", spec.c_str());
	return true;
}

serial_link::~serial_link()
{
	if (m_fd >= 0) { nonblock(m_fd); flush(); close(m_fd); }
	if (m_listen >= 0) close(m_listen);
}

void serial_link::accept_pending()
{
	if (m_fd >= 0 || m_listen < 0) return;
	int fd = accept(m_listen, nullptr, nullptr);
	if (fd < 0) return;
	int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	nonblock(fd);
	m_fd = fd;
	m_out.clear();                // what was sent while nobody listened is lost, like a cable not plugged in
	fprintf(stderr, "[remote port: peer connected]\r\n");
}

void serial_link::send(u8 c)
{
	if (m_fd < 0) return;         // no cable: the byte goes nowhere
	m_out.push_back(c);
	m_tx++;
	if (m_out.size() >= 64) flush();
}

void serial_link::flush()
{
	while (m_fd >= 0 && !m_out.empty()) {
		u8 buf[512]; size_t n = 0;
		while (n < sizeof buf && n < m_out.size()) { buf[n] = m_out[n]; n++; }
		ssize_t w = write(m_fd, buf, n);
		if (w > 0) { m_out.erase(m_out.begin(), m_out.begin() + w); continue; }
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
		drop("write failed");
	}
}

long serial_link::read_some(u8 *buf, size_t n)
{
	ssize_t r = read(m_fd, buf, n);
	if (r > 0) { m_rx += size_t(r); return long(r); }
	if (r == 0) { drop("peer closed"); return 0; }
	if (errno != EAGAIN && errno != EWOULDBLOCK) drop("read failed");
	return 0;
}

void serial_link::drop(const char *why)
{
	fprintf(stderr, "[remote port: %s]\r\n", why);
	close(m_fd); m_fd = -1; m_out.clear();
}
