#ifndef WHP_REMOTE_H_
#define WHP_REMOTE_H_

#include "whp/platform/invitation.h"

#include <cstdint>
#include <span>

namespace whp {

// Cadmium magic-key ordinals are uint32 (`ScrambleMethodOrdinals` & 0x7fffffff).
// SRPC magics (`PackASCII6("SECRPC")` / `VOODOO`) are also fireable; HolePunch
// does not reject them.
inline constexpr uint32_t kMaxMojoOrdinal = 0x7fffffffu;
inline constexpr uint64_t kSECRPC = 91556947316803ull;

inline bool IsMojoOrdinal(uint64_t token) { return token >= 1 && token <= kMaxMojoOrdinal; }
inline bool IsSRPCToken(uint64_t token) { return token > 0xffffffffull; }

// Thin Remote over an invited primordial pipe. Cadmium stays unchanged:
// HolePunch2Cadmium maps `{ordinal, args}` onto Fire().
class Remote {
 public:
  explicit Remote(platform::Invitation* inv) : inv_(inv) {}

  bool is_bound() const { return inv_ && inv_->is_attached(); }

  WhpResult Fire(uint32_t ordinal,
                 std::span<const uint8_t> payload = {},
                 uint32_t flags = Message::kFlagExpectsResponse) {
    if (!inv_) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    return inv_->Fire(ordinal, payload.data(), payload.size(), flags);
  }

  WhpResult FireToken(uint64_t token,
                      std::span<const uint8_t> payload = {},
                      uint32_t flags = Message::kFlagExpectsResponse) {
    return Fire(static_cast<uint32_t>(token), payload, flags);
  }

  WhpResult Recv(Message* out) {
    return inv_ ? inv_->Recv(out) : WHP_RESULT_INVALID_ARGUMENT;
  }

  int Pump(int timeout_ms) { return inv_ ? inv_->Pump(timeout_ms) : 0; }

 private:
  platform::Invitation* inv_ = nullptr;
};

}  // namespace whp

#endif  // WHP_REMOTE_H_
