#ifndef WHP_NET_SOCK_INTERNAL_H_
#define WHP_NET_SOCK_INTERNAL_H_

#include "whp/net/endpoint.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
inline constexpr SocketType kInvalidNativeSocket = INVALID_SOCKET;
inline int LastSockError() { return WSAGetLastError(); }
inline int CloseNativeSocket(SocketType s) { return closesocket(s); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using SocketType = int;
inline constexpr SocketType kInvalidNativeSocket = -1;
inline int LastSockError() { return errno; }
inline int CloseNativeSocket(SocketType s) { return close(s); }
#endif

namespace whp {
namespace net {
namespace internal {

void EnsureNetInit();
sockaddr_in ToSockAddr(const Endpoint& e);
Endpoint FromSockAddr(const sockaddr_in& a);
SocketType AsSocket(uintptr_t native);
uintptr_t FromSocket(SocketType s);

// Windows-only: disable WSAECONNRESET-on-ICMP-port-unreachable for a UDP
// socket (the "SIO_UDP_CONNRESET new behavior"). Without this, a datagram
// sent to a peer that hasn't bound its port yet (the normal case during a
// punch/rendezvous race) triggers an ICMP Port Unreachable that Windows
// surfaces as a hard error (10054) on this socket's *next* send/recv —
// even one addressed to a different, perfectly reachable peer. No-op on
// non-Windows (the condition doesn't exist there).
void DisableUdpConnReset(SocketType s);

}  // namespace internal
}  // namespace net
}  // namespace whp

#endif  // WHP_NET_SOCK_INTERNAL_H_
