#ifndef WHP_NET_POLL_H_
#define WHP_NET_POLL_H_

#include "whp/c/types.h"

#include <cstdint>
#include <vector>

namespace whp {
namespace net {

enum PollEvents : uint32_t {
  kPollIn = 1u << 0,
  kPollOut = 1u << 1,
  kPollErr = 1u << 2,
  kPollHup = 1u << 3,
};

struct PollFd {
  uintptr_t native = static_cast<uintptr_t>(-1);
  uint32_t events = 0;
  uint32_t revents = 0;
};

// timeout_ms < 0 waits forever. Returns number of ready fds, 0 on timeout,
// or -1 on error.
int Poll(std::vector<PollFd>* fds, int timeout_ms);

}  // namespace net
}  // namespace whp

#endif  // WHP_NET_POLL_H_
