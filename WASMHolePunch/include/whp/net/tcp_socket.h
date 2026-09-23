#ifndef WHP_NET_TCP_SOCKET_H_
#define WHP_NET_TCP_SOCKET_H_

#include "whp/c/types.h"
#include "whp/net/endpoint.h"

#include <cstddef>
#include <cstdint>

namespace whp {
namespace net {

class TcpSocket {
 public:
  TcpSocket();
  ~TcpSocket();

  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;
  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  bool is_valid() const { return native_ != kInvalid; }

  WhpResult Bind(const Endpoint& local);
  WhpResult Listen(int backlog = 16);
  WhpResult Accept(TcpSocket* accepted, Endpoint* peer);
  WhpResult Connect(const Endpoint& remote);
  WhpResult SetNonBlocking(bool enabled);
  WhpResult SetReuseAddr(bool enabled);

  int Send(const void* data, size_t n);
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

#endif  // WHP_NET_TCP_SOCKET_H_
