#ifndef WHP_NET_ENDPOINT_H_
#define WHP_NET_ENDPOINT_H_

#include <cstdint>
#include <string>

namespace whp {
namespace net {

struct Endpoint {
  std::string host = "0.0.0.0";
  uint16_t port = 0;

  static Endpoint Any(uint16_t port = 0) { return {"0.0.0.0", port}; }
  static Endpoint Loopback(uint16_t port = 0) { return {"127.0.0.1", port}; }

  bool operator==(const Endpoint& o) const {
    return host == o.host && port == o.port;
  }
};

}  // namespace net
}  // namespace whp

#endif  // WHP_NET_ENDPOINT_H_
