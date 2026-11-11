// Portable stream sockets for the host links (prototype target, REMOTE port):
// BSD sockets on POSIX, Winsock on Windows. Only what the links need.
//   tcp:HOST:PORT   connect          unix:PATH   connect to a Unix-domain socket
//   listen_local(port)               accept on 127.0.0.1
// Unix-domain sockets work on Windows 10 1803+ where the toolchain has <afunix.h>.
#ifndef TEK_HOST_NET_H
#define TEK_HOST_NET_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace net {
using sock = std::intptr_t;          // SOCKET on Windows, int fd elsewhere
constexpr sock BAD = -1;

// connect to "tcp:HOST:PORT" or "unix:PATH" (blocking connect); BAD + err on failure
sock connect_spec(const std::string &spec, std::string &err);
// listening socket on 127.0.0.1:port (non-blocking)
sock listen_local(int port, std::string &err);
// a pending connection on a non-blocking listener, or BAD
sock accept_nb(sock l);
void set_nonblock(sock s);
void set_nodelay(sock s);
// >0 bytes, 0 = peer closed (recv) / nothing written, -1 = error or would block
long send_some(sock s, const void *buf, std::size_t n);
long recv_some(sock s, void *buf, std::size_t n);
bool would_block();                  // the last call failed only because it would block
void close(sock s);
}
#endif
