#include "sock_internal.h"

#include <cstring>

namespace whp {
namespace net {
namespace internal {

#ifdef _WIN32
void EnsureNetInit() {
  static bool started = [] {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    return true;
  }();
  (void)started;
}
#else
void EnsureNetInit() {}
#endif

sockaddr_in ToSockAddr(const Endpoint& e) {
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(e.port);
  if (e.host.empty() || e.host == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    inet_pton(AF_INET, e.host.c_str(), &addr.sin_addr);
  }
  return addr;
}

Endpoint FromSockAddr(const sockaddr_in& a) {
  Endpoint e;
  char buf[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
  e.host = buf;
  e.port = ntohs(a.sin_port);
  return e;
}

SocketType AsSocket(uintptr_t native) {
  return static_cast<SocketType>(native);
}

uintptr_t FromSocket(SocketType s) { return static_cast<uintptr_t>(s); }

#ifdef _WIN32
void DisableUdpConnReset(SocketType s) {
  DWORD bytes_returned = 0;
  BOOL new_behavior = FALSE;
  // SIO_UDP_CONNRESET = _WSAIOW(IOC_VENDOR, 12); avoids pulling in mswsock.h.
  const DWORD kSioUdpConnReset = _WSAIOW(IOC_VENDOR, 12);
  ::WSAIoctl(s, kSioUdpConnReset, &new_behavior, sizeof(new_behavior), nullptr,
             0, &bytes_returned, nullptr, nullptr);
}
#else
void DisableUdpConnReset(SocketType) {}
#endif

}  // namespace internal
}  // namespace net
}  // namespace whp
