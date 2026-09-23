#include "whp/net/poll.h"

#include "sock_internal.h"

#ifndef _WIN32
#include <poll.h>
#endif

namespace whp {
namespace net {

int Poll(std::vector<PollFd>* fds, int timeout_ms) {
  if (!fds) {
    return -1;
  }
  internal::EnsureNetInit();
#ifdef _WIN32
  fd_set readfds, writefds, errfds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&errfds);
  SOCKET maxfd = 0;
  for (auto& fd : *fds) {
    fd.revents = 0;
    SOCKET s = internal::AsSocket(fd.native);
    if (fd.events & kPollIn) {
      FD_SET(s, &readfds);
    }
    if (fd.events & kPollOut) {
      FD_SET(s, &writefds);
    }
    FD_SET(s, &errfds);
    if (s > maxfd) {
      maxfd = s;
    }
  }
  timeval tv;
  timeval* ptv = nullptr;
  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ptv = &tv;
  }
  int rc = select(static_cast<int>(maxfd + 1), &readfds, &writefds, &errfds, ptv);
  if (rc <= 0) {
    return rc;
  }
  int ready = 0;
  for (auto& fd : *fds) {
    SOCKET s = internal::AsSocket(fd.native);
    if (FD_ISSET(s, &readfds)) {
      fd.revents |= kPollIn;
    }
    if (FD_ISSET(s, &writefds)) {
      fd.revents |= kPollOut;
    }
    if (FD_ISSET(s, &errfds)) {
      fd.revents |= kPollErr;
    }
    if (fd.revents) {
      ++ready;
    }
  }
  return ready;
#else
  std::vector<pollfd> pfds(fds->size());
  for (size_t i = 0; i < fds->size(); ++i) {
    (*fds)[i].revents = 0;
    pfds[i].fd = internal::AsSocket((*fds)[i].native);
    pfds[i].events = 0;
    pfds[i].revents = 0;
    if ((*fds)[i].events & kPollIn) {
      pfds[i].events |= POLLIN;
    }
    if ((*fds)[i].events & kPollOut) {
      pfds[i].events |= POLLOUT;
    }
  }
  int rc = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout_ms);
  if (rc <= 0) {
    return rc;
  }
  int ready = 0;
  for (size_t i = 0; i < fds->size(); ++i) {
    if (pfds[i].revents & POLLIN) {
      (*fds)[i].revents |= kPollIn;
    }
    if (pfds[i].revents & POLLOUT) {
      (*fds)[i].revents |= kPollOut;
    }
    if (pfds[i].revents & POLLERR) {
      (*fds)[i].revents |= kPollErr;
    }
    if (pfds[i].revents & POLLHUP) {
      (*fds)[i].revents |= kPollHup;
    }
    if ((*fds)[i].revents) {
      ++ready;
    }
  }
  return ready;
#endif
}

}  // namespace net
}  // namespace whp
