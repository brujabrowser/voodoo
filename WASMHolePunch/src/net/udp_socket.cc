#include "whp/net/udp_socket.h"

#include "sock_internal.h"

namespace whp {
namespace net {

UdpSocket::UdpSocket() {
  internal::EnsureNetInit();
  SocketType s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s != kInvalidNativeSocket) {
    internal::DisableUdpConnReset(s);
    native_ = internal::FromSocket(s);
  }
}

UdpSocket UdpSocket::Adopt(uintptr_t native) {
  UdpSocket sock;
  sock.Close();
  sock.native_ = native;
  return sock;
}

UdpSocket::~UdpSocket() { Close(); }

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : native_(other.native_), last_error_(other.last_error_) {
  other.native_ = kInvalid;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    Close();
    native_ = other.native_;
    last_error_ = other.last_error_;
    other.native_ = kInvalid;
  }
  return *this;
}

void UdpSocket::Close() {
  if (native_ != kInvalid) {
    CloseNativeSocket(internal::AsSocket(native_));
    native_ = kInvalid;
  }
}

WhpResult UdpSocket::Bind(const Endpoint& local) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  sockaddr_in addr = internal::ToSockAddr(local);
  if (::bind(internal::AsSocket(native_),
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult UdpSocket::Connect(const Endpoint& remote) {
  if (!is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  sockaddr_in addr = internal::ToSockAddr(remote);
  if (::connect(internal::AsSocket(native_),
                reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    last_error_ = LastSockError();
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult UdpSocket::SetNonBlocking(bool enabled) {
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

WhpResult UdpSocket::SetReuseAddr(bool enabled) {
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

int UdpSocket::SendTo(const void* data, size_t n, const Endpoint& dest) {
  if (!is_valid()) {
    last_error_ = 0;
    return -1;
  }
  sockaddr_in addr = internal::ToSockAddr(dest);
  int sent = ::sendto(internal::AsSocket(native_),
                      static_cast<const char*>(data), static_cast<int>(n), 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (sent < 0) {
    last_error_ = LastSockError();
  }
  return sent;
}

int UdpSocket::Send(const void* data, size_t n) {
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

int UdpSocket::RecvFrom(void* data, size_t n, Endpoint* src) {
  if (!is_valid()) {
    return -1;
  }
  sockaddr_in addr;
  socklen_t len = sizeof(addr);
  int got = ::recvfrom(internal::AsSocket(native_), static_cast<char*>(data),
                       static_cast<int>(n), 0, reinterpret_cast<sockaddr*>(&addr),
                       &len);
  if (got < 0) {
    last_error_ = LastSockError();
    return got;
  }
  if (src) {
    *src = internal::FromSockAddr(addr);
  }
  return got;
}

int UdpSocket::Recv(void* data, size_t n) {
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

Endpoint UdpSocket::LocalEndpoint() const {
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

Endpoint UdpSocket::RemoteEndpoint() const {
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
