#include "whp/punch/punch.h"

#include "whp/net/poll.h"

#include <cstring>

namespace whp {
namespace punch {

std::vector<Candidate> Gather(net::UdpSocket& sock) {
  net::Endpoint e = sock.LocalEndpoint();
  if (e.host.empty() || e.host == "0.0.0.0") {
    e.host = "127.0.0.1";
  }
  return {Candidate{e, CandidateType::kHost}};
}

std::vector<Candidate> Stun(net::UdpSocket&, const net::Endpoint&) {
  return {};
}

WhpResult Punch(net::UdpSocket& local,
                const std::vector<Candidate>& remote,
                ConnectedPath* out) {
  if (!out || !local.is_valid() || remote.empty()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  local.SetNonBlocking(true);
  constexpr char kMagic[] = {'W', 'H', 'P', '1'};
  net::Endpoint peer;
  bool got = false;
  for (int attempt = 0; attempt < 40 && !got; ++attempt) {
    for (const auto& c : remote) {
      local.SendTo(kMagic, sizeof(kMagic), c.endpoint);
    }
    net::PollFd pfd;
    pfd.native = local.native();
    pfd.events = net::kPollIn;
    std::vector<net::PollFd> fds = {pfd};
    if (net::Poll(&fds, 25) <= 0) {
      continue;
    }
    char buf[16];
    net::Endpoint src;
    int n = local.RecvFrom(buf, sizeof(buf), &src);
    if (n >= 4 && std::memcmp(buf, kMagic, 4) == 0) {
      peer = src;
      got = true;
    }
  }
  if (!got) {
    local.SetNonBlocking(false);
    return WHP_RESULT_DEADLINE_EXCEEDED;
  }
  // Echo magic once more so the other side can complete.
  local.SendTo(kMagic, sizeof(kMagic), peer);
  if (local.Connect(peer) != WHP_RESULT_OK) {
    local.SetNonBlocking(false);
    return WHP_RESULT_UNKNOWN;
  }
  // Drop leftover handshake datagrams so the caller sees an empty path.
  // A single burst-drain (recv-until-empty, no waiting) isn't enough: the
  // peer's own punch loop can still be retransmitting kMagic for a few more
  // milliseconds after *this* side already saw its match and connected, so
  // a straggler can arrive microseconds after the burst-drain already found
  // the queue empty. Every direct consumer of ConnectedPath has to treat
  // that as reachable (Invitation::RecvFrame does), but it's cheaper and
  // more robust to just wait out a short quiet window here, once, at the
  // source, instead of asking every future caller to defend against it.
  char drain[16];
  for (int quiet = 0; quiet < 3;) {
    net::PollFd pfd;
    pfd.native = local.native();
    pfd.events = net::kPollIn;
    std::vector<net::PollFd> fds = {pfd};
    if (net::Poll(&fds, 15) <= 0) {
      ++quiet;
      continue;
    }
    quiet = 0;
    local.Recv(drain, sizeof(drain));
  }
  local.SetNonBlocking(false);
  *out = ConnectedPath(std::move(local), peer);
  return WHP_RESULT_OK;
}

}  // namespace punch
}  // namespace whp
