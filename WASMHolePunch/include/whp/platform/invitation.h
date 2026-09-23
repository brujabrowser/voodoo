#ifndef WHP_PLATFORM_INVITATION_H_
#define WHP_PLATFORM_INVITATION_H_

#include "whp/c/types.h"
#include "whp/message.h"
#include "whp/punch/punch.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace whp {
namespace platform {

// Length-prefixed (really: one-datagram) broker frames over a punched UDP
// path. Bootstraps one primordial message pipe. ipcz-lite:
//
//   WHP2 invite/accept  →  Mojo V3 frames  →  local message pipe
//
// Cadmium does not own this. HolePunch2Cadmium holds an Invitation and
// maps Cadmium `/api/rbi/ipc` `{ordinal,args}` onto Fire().

class Invitation {
 public:
  Invitation() = default;
  ~Invitation();

  Invitation(Invitation&& other) noexcept;
  Invitation& operator=(Invitation&& other) noexcept;
  Invitation(const Invitation&) = delete;
  Invitation& operator=(const Invitation&) = delete;

  WhpResult Attach(punch::ConnectedPath path, bool is_offerer);
  bool is_attached() const { return local_ != WHP_HANDLE_INVALID && path_.is_valid(); }
  bool is_offerer() const { return offerer_; }
  WhpHandle local_pipe() const { return local_; }

  // Flush local writes onto UDP and ingest inbound frames. Returns frames
  // handled. timeout_ms=0 is non-blocking.
  int Pump(int timeout_ms);

  // Write a Mojo RPC onto the primordial pipe (`header.name` = ordinal).
  WhpResult Fire(uint32_t ordinal,
                 const void* payload = nullptr,
                 size_t payload_n = 0,
                 uint32_t flags = Message::kFlagExpectsResponse);

  // Non-blocking read of one inbound message (pumps first).
  WhpResult Recv(Message* out);

  void Close();

 private:
  static constexpr char kMagic[4] = {'W', 'H', 'P', '2'};
  enum FrameType : uint8_t { kInvite = 1, kAccept = 2, kMojo = 3, kClose = 4 };

  WhpResult Handshake(bool is_offerer);
  WhpResult SendFrame(uint8_t type, const void* payload, size_t n);
  WhpResult RecvFrame(uint8_t* type, std::vector<uint8_t>* payload, int timeout_ms);
  int FlushPipeToNet();
  int IngestNet(int timeout_ms);
  void HandleControl(const uint8_t* bytes, size_t n);
  void SendRunResponse(uint64_t request_id);

  punch::ConnectedPath path_;
  WhpHandle local_ = WHP_HANDLE_INVALID;
  WhpHandle net_ = WHP_HANDLE_INVALID;
  uint64_t next_request_id_ = 1;
  bool offerer_ = false;
  bool closed_ = false;
};

// Attach `path` to `session`. `session` must outlive the pipe.
WhpResult InviteOver(punch::ConnectedPath path,
                     Invitation* session,
                     bool is_offerer);

}  // namespace platform
}  // namespace whp

#endif  // WHP_PLATFORM_INVITATION_H_
