#include "whp/platform/invitation.h"

#include "whp/control.h"
#include "whp/interface_id.h"
#include "whp/net/poll.h"

#include <cstring>
#include <span>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <errno.h>
#endif

namespace whp {
namespace platform {
namespace {

bool WouldBlock(int err) {
#ifdef _WIN32
  return err == WSAEWOULDBLOCK;
#else
  return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

void WriteU16LE(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t ReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

const internal::MessageHeaderV3* AsV3(const uint8_t* bytes, size_t n) {
  if (n < sizeof(internal::MessageHeaderV3)) {
    return nullptr;
  }
  return reinterpret_cast<const internal::MessageHeaderV3*>(bytes);
}

}  // namespace

Invitation::~Invitation() { Close(); }

Invitation::Invitation(Invitation&& other) noexcept
    : path_(std::move(other.path_)),
      local_(other.local_),
      net_(other.net_),
      next_request_id_(other.next_request_id_),
      offerer_(other.offerer_),
      closed_(other.closed_) {
  other.local_ = WHP_HANDLE_INVALID;
  other.net_ = WHP_HANDLE_INVALID;
  other.closed_ = true;
}

Invitation& Invitation::operator=(Invitation&& other) noexcept {
  if (this != &other) {
    Close();
    path_ = std::move(other.path_);
    local_ = other.local_;
    net_ = other.net_;
    next_request_id_ = other.next_request_id_;
    offerer_ = other.offerer_;
    closed_ = other.closed_;
    other.local_ = WHP_HANDLE_INVALID;
    other.net_ = WHP_HANDLE_INVALID;
    other.closed_ = true;
  }
  return *this;
}

void Invitation::Close() {
  if (closed_ && local_ == WHP_HANDLE_INVALID && net_ == WHP_HANDLE_INVALID) {
    return;
  }
  if (path_.is_valid() && !closed_) {
    SendFrame(kClose, nullptr, 0);
  }
  closed_ = true;
  if (local_ != WHP_HANDLE_INVALID) {
    WhpClose(local_);
    local_ = WHP_HANDLE_INVALID;
  }
  if (net_ != WHP_HANDLE_INVALID) {
    WhpClose(net_);
    net_ = WHP_HANDLE_INVALID;
  }
}

WhpResult Invitation::SendFrame(uint8_t type, const void* payload, size_t n) {
  if (!path_.is_valid() || n > 0xffff) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  uint8_t hdr[8];
  std::memcpy(hdr, kMagic, 4);
  hdr[4] = type;
  hdr[5] = 0;
  WriteU16LE(hdr + 6, static_cast<uint16_t>(n));
  std::vector<uint8_t> buf;
  buf.resize(8 + n);
  std::memcpy(buf.data(), hdr, 8);
  if (n && payload) {
    std::memcpy(buf.data() + 8, payload, n);
  }
  int sent = path_.Send(buf.data(), buf.size());
  if (sent != static_cast<int>(buf.size())) {
    return WHP_RESULT_UNKNOWN;
  }
  return WHP_RESULT_OK;
}

WhpResult Invitation::RecvFrame(uint8_t* type,
                                std::vector<uint8_t>* payload,
                                int timeout_ms) {
  if (!type || !payload || !path_.is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  path_.socket().SetNonBlocking(true);
  net::PollFd pfd;
  pfd.native = path_.socket().native();
  pfd.events = net::kPollIn;
  std::vector<net::PollFd> fds = {pfd};
  int pr = net::Poll(&fds, timeout_ms);
  if (pr == 0) {
    return WHP_RESULT_SHOULD_WAIT;
  }
  if (pr < 0) {
    return WHP_RESULT_UNKNOWN;
  }
  uint8_t buf[65536];
  int got = path_.Recv(buf, sizeof(buf));
  if (got < 0) {
    if (WouldBlock(path_.socket().last_error())) {
      return WHP_RESULT_SHOULD_WAIT;
    }
    return WHP_RESULT_UNKNOWN;
  }
  if (got < 8 || std::memcmp(buf, kMagic, 4) != 0) {
    // Too short to even hold our 8-byte header, or the header's present
    // but the magic isn't "WHP2" — either way this isn't a real invitation
    // frame. Most likely a straggler from punch::Punch()'s bare 4-byte
    // "WHP1" handshake landing on the same connected socket after Punch()
    // returned (its post-Connect drain is a best-effort burst, not a
    // guarantee against in-flight retransmits — a `got < 8` datagram is
    // exactly what that raw punch magic looks like, header and all).
    // Treat as noise to drop, not a fatal error: keep waiting for a real
    // frame within the caller's timeout budget instead of aborting.
    return WHP_RESULT_SHOULD_WAIT;
  }
  *type = buf[4];
  uint16_t len = ReadU16LE(buf + 6);
  if (8 + static_cast<int>(len) > got) {
    return WHP_RESULT_DATA_LOSS;
  }
  payload->assign(buf + 8, buf + 8 + len);
  return WHP_RESULT_OK;
}

WhpResult Invitation::Handshake(bool is_offerer) {
  uint8_t role = is_offerer ? 1 : 0;
  if (is_offerer) {
    // UDP is unreliable: a single kInvite send can be lost outright (most
    // commonly the very first datagram on a socket that just came out of
    // punch::Punch()'s Connect()). Resend periodically instead of sending
    // once and passively waiting out the whole budget — same reasoning as
    // punch::Punch()'s own per-attempt resend of its rendezvous magic.
    if (SendFrame(kInvite, &role, 1) != WHP_RESULT_OK) {
      return WHP_RESULT_UNKNOWN;
    }
    for (int i = 0; i < 80; ++i) {
      if (i > 0 && i % 8 == 0) {
        (void)SendFrame(kInvite, &role, 1);
      }
      uint8_t type = 0;
      std::vector<uint8_t> payload;
      WhpResult r = RecvFrame(&type, &payload, 25);
      if (r == WHP_RESULT_SHOULD_WAIT) {
        continue;
      }
      if (r != WHP_RESULT_OK) {
        return r;
      }
      if (type == kAccept) {
        return WHP_RESULT_OK;
      }
      if (type == kInvite) {
        // Simultaneous open: the other side also offered. Accept anyway.
        (void)SendFrame(kAccept, &role, 1);
        return WHP_RESULT_OK;
      }
    }
    return WHP_RESULT_DEADLINE_EXCEEDED;
  }
  for (int i = 0; i < 80; ++i) {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
    WhpResult r = RecvFrame(&type, &payload, 25);
    if (r == WHP_RESULT_SHOULD_WAIT) {
      continue;
    }
    if (r != WHP_RESULT_OK) {
      return r;
    }
    if (type == kInvite) {
      return SendFrame(kAccept, &role, 1);
    }
  }
  return WHP_RESULT_DEADLINE_EXCEEDED;
}

WhpResult Invitation::Attach(punch::ConnectedPath path, bool is_offerer) {
  if (!path.is_valid()) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Close();
  closed_ = false;
  offerer_ = is_offerer;
  path_ = std::move(path);
  WhpInit();
  if (WhpCreateMessagePipe(nullptr, &local_, &net_) != WHP_RESULT_OK) {
    return WHP_RESULT_INTERNAL;
  }
  WhpResult hs = Handshake(is_offerer);
  if (hs != WHP_RESULT_OK) {
    Close();
    return hs;
  }
  path_.socket().SetNonBlocking(true);
  return WHP_RESULT_OK;
}

int Invitation::FlushPipeToNet() {
  if (net_ == WHP_HANDLE_INVALID) {
    return 0;
  }
  int n = 0;
  for (;;) {
    WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
    WhpResult r = WhpReadMessage(net_, nullptr, &msg);
    if (r == WHP_RESULT_SHOULD_WAIT) {
      break;
    }
    if (r != WHP_RESULT_OK) {
      break;
    }
    void* buf = nullptr;
    uint32_t sz = 0;
    if (WhpGetMessageData(msg, nullptr, &buf, &sz, nullptr, nullptr) ==
            WHP_RESULT_OK &&
        buf && sz) {
      if (SendFrame(kMojo, buf, sz) == WHP_RESULT_OK) {
        ++n;
      }
    }
    WhpDestroyMessage(msg);
  }
  return n;
}

void Invitation::SendRunResponse(uint64_t request_id) {
  Message reply(control::kRunMessageId, Message::kFlagIsResponse, 12);
  reply.set_interface_id(kPrimaryInterfaceId);
  reply.set_request_id(request_id);
  uint8_t* p = reply.mutable_payload();
  if (p) {
    auto* hdr = reinterpret_cast<internal::StructHeader*>(p);
    hdr->num_bytes = 12;
    hdr->version = 0;
    uint32_t ver = control::kBindingsVersion;
    std::memcpy(p + 8, &ver, 4);
  }
  auto* bytes = reply.data();
  size_t n = reply.data_num_bytes();
  if (bytes && n) {
    SendFrame(kMojo, bytes, n);
  }
}

void Invitation::HandleControl(const uint8_t* bytes, size_t n) {
  const auto* h = AsV3(bytes, n);
  if (!h) {
    return;
  }
  if (h->name != control::kRunMessageId) {
    return;
  }
  if (h->flags & Message::kFlagIsResponse) {
    return;
  }
  SendRunResponse(h->request_id);
}

int Invitation::IngestNet(int timeout_ms) {
  int n = 0;
  bool first = true;
  for (;;) {
    uint8_t type = 0;
    std::vector<uint8_t> payload;
    int wait = first ? timeout_ms : 0;
    first = false;
    WhpResult r = RecvFrame(&type, &payload, wait);
    if (r == WHP_RESULT_SHOULD_WAIT) {
      break;
    }
    if (r != WHP_RESULT_OK) {
      break;
    }
    ++n;
    if (type == kClose) {
      if (net_ != WHP_HANDLE_INVALID) {
        WhpClose(net_);
        net_ = WHP_HANDLE_INVALID;
      }
      break;
    }
    if (type != kMojo || payload.empty() || net_ == WHP_HANDLE_INVALID) {
      continue;
    }
    HandleControl(payload.data(), payload.size());
    const auto* h = AsV3(payload.data(), payload.size());
    const bool swallow_run =
        h && h->name == control::kRunMessageId &&
        (h->flags & Message::kFlagIsResponse) == 0;
    if (swallow_run) {
      continue;
    }
    Message msg(std::span<const uint8_t>(payload.data(), payload.size()),
                std::span<WhpHandle>{});
    WhpMessageHandle raw = msg.TakeMojoMessage();
    if (WhpWriteMessage(net_, raw, nullptr) != WHP_RESULT_OK) {
      WhpDestroyMessage(raw);
    }
  }
  return n;
}

int Invitation::Pump(int timeout_ms) {
  if (!is_attached()) {
    return 0;
  }
  int n = FlushPipeToNet();
  n += IngestNet(timeout_ms);
  n += FlushPipeToNet();
  return n;
}

WhpResult Invitation::Fire(uint32_t ordinal,
                           const void* payload,
                           size_t payload_n,
                           uint32_t flags) {
  if (!is_attached()) {
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  Message msg(ordinal, flags, payload_n);
  msg.set_interface_id(kPrimaryInterfaceId);
  msg.set_request_id(next_request_id_++);
  if (payload_n && payload && msg.mutable_payload()) {
    std::memcpy(msg.mutable_payload(), payload, payload_n);
  }
  WhpMessageHandle h = msg.TakeMojoMessage();
  WhpResult r = WhpWriteMessage(local_, h, nullptr);
  if (r != WHP_RESULT_OK) {
    if (r == WHP_RESULT_INVALID_ARGUMENT) {
      WhpDestroyMessage(h);
    }
    return r;
  }
  FlushPipeToNet();
  return WHP_RESULT_OK;
}

WhpResult Invitation::Recv(Message* out) {
  if (!out) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Pump(0);
  if (local_ == WHP_HANDLE_INVALID) {
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  WhpMessageHandle h = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpReadMessage(local_, nullptr, &h);
  if (r != WHP_RESULT_OK) {
    return r;
  }
  *out = Message::CreateFromMessageHandle(&h);
  return WHP_RESULT_OK;
}

WhpResult InviteOver(punch::ConnectedPath path,
                     Invitation* session,
                     bool is_offerer) {
  if (!session) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  return session->Attach(std::move(path), is_offerer);
}

}  // namespace platform
}  // namespace whp
