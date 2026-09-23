#include "whp/c/net.h"

#include "whp/net/udp_socket.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

struct NetTable {
  std::mutex mu;
  std::vector<std::unique_ptr<whp::net::UdpSocket>> socks;  // [0] unused
  std::vector<WhpNetSocket> free_list;
  NetTable() { socks.resize(1); }
};

NetTable& Table() {
  static NetTable t;
  return t;
}

whp::net::UdpSocket* Lookup(WhpNetSocket s) {
  auto& t = Table();
  if (s == 0 || s >= t.socks.size() || !t.socks[s]) {
    return nullptr;
  }
  return t.socks[s].get();
}

}  // namespace

extern "C" {

WhpResult WhpNetInit(void) {
  (void)Table();
  return WHP_RESULT_OK;
}

WhpResult WhpNetUdpOpen(WhpNetSocket* out) {
  if (!out) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto sock = std::make_unique<whp::net::UdpSocket>();
  if (!sock->is_valid()) {
    return WHP_RESULT_UNKNOWN;
  }
  auto& t = Table();
  std::lock_guard<std::mutex> lock(t.mu);
  WhpNetSocket id;
  if (!t.free_list.empty()) {
    id = t.free_list.back();
    t.free_list.pop_back();
    t.socks[id] = std::move(sock);
  } else {
    id = static_cast<WhpNetSocket>(t.socks.size());
    t.socks.push_back(std::move(sock));
  }
  *out = id;
  return WHP_RESULT_OK;
}

WhpResult WhpNetClose(WhpNetSocket socket) {
  auto& t = Table();
  std::lock_guard<std::mutex> lock(t.mu);
  if (socket == 0 || socket >= t.socks.size() || !t.socks[socket]) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  t.socks[socket].reset();
  t.free_list.push_back(socket);
  return WHP_RESULT_OK;
}

WhpResult WhpNetBind(WhpNetSocket socket, const char* host, uint16_t port) {
  auto* s = Lookup(socket);
  if (!s) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  whp::net::Endpoint e;
  e.host = host ? host : "0.0.0.0";
  e.port = port;
  return s->Bind(e);
}

WhpResult WhpNetSetNonBlocking(WhpNetSocket socket, int enabled) {
  auto* s = Lookup(socket);
  if (!s) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  return s->SetNonBlocking(enabled != 0);
}

WhpResult WhpNetSetReuseAddr(WhpNetSocket socket, int enabled) {
  auto* s = Lookup(socket);
  if (!s) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  return s->SetReuseAddr(enabled != 0);
}

int WhpNetSendTo(WhpNetSocket socket,
                 const void* data,
                 uint32_t n,
                 const char* host,
                 uint16_t port) {
  auto* s = Lookup(socket);
  if (!s) {
    return -1;
  }
  whp::net::Endpoint e;
  e.host = host ? host : "127.0.0.1";
  e.port = port;
  return s->SendTo(data, n, e);
}

int WhpNetRecvFrom(WhpNetSocket socket,
                   void* data,
                   uint32_t n,
                   char* host,
                   uint32_t host_len,
                   uint16_t* port) {
  auto* s = Lookup(socket);
  if (!s) {
    return -1;
  }
  whp::net::Endpoint src;
  int got = s->RecvFrom(data, n, &src);
  if (got >= 0) {
    if (host && host_len) {
      std::snprintf(host, host_len, "%s", src.host.c_str());
    }
    if (port) {
      *port = src.port;
    }
  }
  return got;
}

WhpResult WhpNetLocalEndpoint(WhpNetSocket socket,
                              char* host,
                              uint32_t host_len,
                              uint16_t* port) {
  auto* s = Lookup(socket);
  if (!s) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto e = s->LocalEndpoint();
  if (host && host_len) {
    std::snprintf(host, host_len, "%s", e.host.c_str());
  }
  if (port) {
    *port = e.port;
  }
  return WHP_RESULT_OK;
}

}  // extern "C"
