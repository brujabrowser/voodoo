#ifndef WHP_NET_UDP_SOCKET_H_
#define WHP_NET_UDP_SOCKET_H_

#include "whp/c/types.h"
#include "whp/net/endpoint.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace whp {
namespace net {

class UdpSocket {
 public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  bool is_valid() const { return native_ != kInvalid; }

  // Take ownership of an existing sysroot socket fd / SOCKET (wasigocvm
  // POSIX or native). Does not create a new socket.
  static UdpSocket Adopt(uintptr_t native);

  WhpResult Bind(const Endpoint& local);
  WhpResult Connect(const Endpoint& remote);
  WhpResult SetNonBlocking(bool enabled);
  WhpResult SetReuseAddr(bool enabled);

  // Returns bytes sent, or -1 on error (see last_error()).
  int SendTo(const void* data, size_t n, const Endpoint& dest);
  int Send(const void* data, size_t n);
  int RecvFrom(void* data, size_t n, Endpoint* src);
  int Recv(void* data, size_t n);

  Endpoint LocalEndpoint() const;
  Endpoint RemoteEndpoint() const;

  uintptr_t native() const { return native_; }
  int last_error() const { return last_error_; }

  void Close();

 private:
  static constexpr uintptr_t kInvalid = static_cast<uintptr_t>(-1);
  uintptr_t native_ = kInvalid;
  int last_error_ = 0;
};

}  // namespace net
}  // namespace whp

#endif  // WHP_NET_UDP_SOCKET_H_
