#include "whp/net/udp_socket.h"

#include <cstdio>
#include <cstring>

int main() {
  whp::net::UdpSocket a;
  whp::net::UdpSocket b;
  if (a.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK ||
      b.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK) {
    std::fprintf(stderr, "bind failed\n");
    return 1;
  }
  auto eb = b.LocalEndpoint();
  const char* msg = "whp-sockets";
  a.SendTo(msg, std::strlen(msg), eb);
  char buf[64] = {};
  whp::net::Endpoint src;
  int n = b.RecvFrom(buf, sizeof(buf), &src);
  std::printf("recv %d from %s:%u: %.*s\n", n, src.host.c_str(), src.port, n,
              buf);
  return n > 0 ? 0 : 1;
}
