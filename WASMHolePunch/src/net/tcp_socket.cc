#include "whp/net/tcp_socket.h"

#include "sock_internal.h"

namespace whp {
namespace net {

TcpSocket::TcpSocket() {
  internal::EnsureNetInit();
  SocketType s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s != kInvalidNativeSocket) {
    native_ = internal::FromSocket(s);
  }
}

TcpSocket::~TcpSocket() { Close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : native_(other.native_), last_error_(other.last_error_) {
  other.native_ = kInvalid;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    Close();
    native_ = other.native_;
    last_error_ = other.last_error_;
    other.native_ = kInvalid;
  }
  return *this;
}

void TcpSocket::Close() {
  if (native_ != kInvalid) {
    CloseNativeSocket(internal::AsSocket(native_));
    native_ = kInvalid;
  }
}

WhpResult TcpSocket::Bind(const Endpoint& local) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  sockaddr_in addr = internal::ToSockAddr(local);
  if (::bind(internal::AsSocket(native_), reinterpret_cast<sockaddr*>(&addr),
             sizeof(addr)) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult TcpSocket::Listen(int backlog) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  if (::listen(internal::AsSocket(native_), backlog) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult TcpSocket::Accept(TcpSocket* accepted, Endpoint* peer) {
  if (!is_valid() || !accepted) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  SocketType s = ::accept(internal::AsSocket(native_),
                          reinterpret_cast<sockaddr*>(&addr), &len);
  if (s == kInvalidNativeSocket) {
    last_error_ = LastSockError();
    return WHP_RESULT_SHOULD_WAIT;
  }
  accepted->Close();
  accepted->native_ = internal::FromSocket(s);
  if (peer) {
    *peer = internal::FromSockAddr(addr);
  }
  return WHP_RESULT_OK;
}

WhpResult TcpSocket::Connect(const Endpoint& remote) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  sockaddr_in addr = internal::ToSockAddr(remote);
  if (::connect(internal::AsSocket(native_), reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)) != 0) {
    last_error_ = LastSockError();
#ifdef _WIN32
    if (last_error_ == WSAEWOULDBLOCK || last_error_ == WSAEINPROGRESS) {
      return WHP_RESULT_SHOULD_WAIT;
    }
#else
    if (last_error_ == EINPROGRESS || last_error_ == EAGAIN) {
      return WHP_RESULT_SHOULD_WAIT;
    }
#endif
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult TcpSocket::SetNonBlocking(bool enabled) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
#ifdef _WIN32
  u_long mode = enabled ? 1 : 0;
  if (ioctlsocket(internal::AsSocket(native_), FIONBIO, &mode) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
#else
  int flags = fcntl(internal::AsSocket(native_), F_GETFL, 0);
  if (flags < 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  if (enabled) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  if (fcntl(internal::AsSocket(native_), F_SETFL, flags) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
#endif
  return WHP_RESULT_OK;
}

WhpResult TcpSocket::SetReuseAddr(bool enabled) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  int on = enabled ? 1 : 0;
  if (setsockopt(internal::AsSocket(native_), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&on), sizeof(on)) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

int TcpSocket::Send(const void* data, size_t n) {
  if (!is_valid()) {
    return -1;
  }
  int sent = ::send(internal::AsSocket(native_), static_cast<const char*>(data),
                    static_cast<int>(n), 0);
  if (sent < 0) {
    last_error_ = LastSockError();
  }
  return sent;
}

int TcpSocket::Recv(void* data, size_t n) {
  if (!is_valid()) {
    return -1;
  }
  int got = ::recv(internal::AsSocket(native_), static_cast<char*>(data),
                   static_cast<int>(n), 0);
  if (got < 0) {
    last_error_ = LastSockError();
  }
  return got;
}

Endpoint TcpSocket::LocalEndpoint() const {
  Endpoint e;
  if (!is_valid()) {
    return e;
  }
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getsockname(internal::AsSocket(native_),
                  reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
    e = internal::FromSockAddr(addr);
  }
  return e;
}

Endpoint TcpSocket::RemoteEndpoint() const {
  Endpoint e;
  if (!is_valid()) {
    return e;
  }
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if (getpeername(internal::AsSocket(native_),
                  reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
    e = internal::FromSockAddr(addr);
  }
  return e;
}

}  // namespace net
}  // namespace whp
