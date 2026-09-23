#ifndef WHP_PUNCH_PUNCH_H_
#define WHP_PUNCH_PUNCH_H_

#include "whp/c/types.h"
#include "whp/net/endpoint.h"
#include "whp/net/udp_socket.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace whp {
namespace punch {

enum class CandidateType { kHost, kSrflx };

struct Candidate {
  net::Endpoint endpoint;
  CandidateType type = CandidateType::kHost;
};

// Connected UDP (or later TCP) path produced by a successful punch.
class ConnectedPath {
 public:
  ConnectedPath() = default;
  explicit ConnectedPath(net::UdpSocket sock, net::Endpoint peer)
      : sock_(std::move(sock)), peer_(peer) {}

  bool is_valid() const { return sock_.is_valid(); }
  net::UdpSocket& socket() { return sock_; }
  const net::Endpoint& peer() const { return peer_; }

  int Send(const void* data, size_t n) { return sock_.Send(data, n); }
  int Recv(void* data, size_t n) { return sock_.Recv(data, n); }

 private:
  net::UdpSocket sock_;
  net::Endpoint peer_;
};

using SendSignal = std::function<void(std::string bytes)>;

std::vector<Candidate> Gather(net::UdpSocket& sock);

// STUN binding against `stun_server`. Returns empty on failure.
std::vector<Candidate> Stun(net::UdpSocket& sock,
                            const net::Endpoint& stun_server);

// Simultaneous-open punch. Signaling is the caller's job: pass the remote
// candidates you already exchanged. Loopback two-sockets works without STUN.
WhpResult Punch(net::UdpSocket& local,
                const std::vector<Candidate>& remote,
                ConnectedPath* out);

}  // namespace punch
}  // namespace whp

#endif  // WHP_PUNCH_PUNCH_H_
