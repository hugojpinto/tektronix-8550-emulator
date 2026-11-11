// Portable stream sockets -- see net.h
#include "host/net.h"
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#if __has_include(<afunix.h>)
#include <afunix.h>
#define TEK_HAVE_AF_UNIX 1
#endif
namespace {
struct wsa_init { wsa_init() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); } ~wsa_init() { WSACleanup(); } };
void startup() { static wsa_init once; }
SOCKET S(net::sock s) { return SOCKET(s); }
}
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#define TEK_HAVE_AF_UNIX 1
namespace {
void startup() {}
int S(net::sock s) { return int(s); }
}
#endif

namespace net {

void close(sock s)
{
	if (s == BAD) return;
#ifdef _WIN32
	closesocket(S(s));
#else
	::close(S(s));
#endif
}

void set_nonblock(sock s)
{
#ifdef _WIN32
	u_long one = 1; ioctlsocket(S(s), FIONBIO, &one);
#else
	fcntl(S(s), F_SETFL, fcntl(S(s), F_GETFL) | O_NONBLOCK);
#endif
}

void set_nodelay(sock s)
{
	int one = 1;
	setsockopt(S(s), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
}

bool would_block()
{
#ifdef _WIN32
	return WSAGetLastError() == WSAEWOULDBLOCK;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

long send_some(sock s, const void *buf, std::size_t n)
{
#ifdef _WIN32
	return long(::send(S(s), static_cast<const char *>(buf), int(n), 0));
#else
	return long(::write(S(s), buf, n));
#endif
}

long recv_some(sock s, void *buf, std::size_t n)
{
#ifdef _WIN32
	return long(::recv(S(s), static_cast<char *>(buf), int(n), 0));
#else
	return long(::read(S(s), buf, n));
#endif
}

sock connect_spec(const std::string &spec, std::string &err)
{
	startup();
	if (spec.rfind("unix:", 0) == 0) {
#ifdef TEK_HAVE_AF_UNIX
		sockaddr_un sa{}; sa.sun_family = AF_UNIX;
		std::string path = spec.substr(5);
		if (path.size() >= sizeof sa.sun_path) { err = "socket path too long"; return BAD; }
		std::strcpy(sa.sun_path, path.c_str());
		sock s = sock(socket(AF_UNIX, SOCK_STREAM, 0));
		if (s == BAD || ::connect(S(s), reinterpret_cast<sockaddr *>(&sa), sizeof sa) != 0) { err = "cannot connect to " + spec; close(s); return BAD; }
		return s;
#else
		err = "unix: sockets are not available in this build; use tcp:HOST:PORT"; return BAD;
#endif
	}
	if (spec.rfind("tcp:", 0) == 0) {
		std::string hp = spec.substr(4); size_t c = hp.rfind(':');
		if (c == std::string::npos) { err = "tcp spec must be tcp:host:port"; return BAD; }
		addrinfo hints{}, *res = nullptr; hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(hp.substr(0, c).c_str(), hp.substr(c + 1).c_str(), &hints, &res) != 0 || !res) { err = "cannot resolve " + spec; return BAD; }
		sock s = sock(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
		bool ok = s != BAD && ::connect(S(s), res->ai_addr, socklen_t(res->ai_addrlen)) == 0;
		freeaddrinfo(res);
		if (!ok) { err = "cannot connect to " + spec; close(s); return BAD; }
		set_nodelay(s);
		return s;
	}
	err = "spec must be tcp:HOST:PORT or unix:PATH";
	return BAD;
}

sock listen_local(int port, std::string &err)
{
	startup();
	sock l = sock(socket(AF_INET, SOCK_STREAM, 0));
	int one = 1;
	if (l != BAD) setsockopt(S(l), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);
	sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(uint16_t(port)); sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (l == BAD || bind(S(l), reinterpret_cast<sockaddr *>(&sa), sizeof sa) != 0 || listen(S(l), 1) != 0) {
		err = "cannot listen on 127.0.0.1:" + std::to_string(port); close(l); return BAD;
	}
	set_nonblock(l);
	return l;
}

sock accept_nb(sock l)
{
	sock s = sock(accept(S(l), nullptr, nullptr));
	if (s == BAD) return BAD;
	set_nodelay(s);
	set_nonblock(s);
	return s;
}

}
