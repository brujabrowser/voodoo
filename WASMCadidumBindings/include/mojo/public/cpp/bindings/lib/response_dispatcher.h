// A small, interface-agnostic piece of runtime plumbing that a generated
// (or hand-written, .voodoom-style) Proxy_ composes to match outgoing
// [ExpectsResponse] calls with their eventual response Message, keyed by
// Message::request_id(). Each pending entry is itself the closure that
// knows how to decode *that* response -- the dispatcher never needs to
// understand any interface's wire format, only route by id. This is the
// same role Chromium's InterfaceEndpointClient plays for pending calls.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_RESPONSE_DISPATCHER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_RESPONSE_DISPATCHER_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/message.h"

#include <unordered_map>

namespace mojo::internal {

class ResponseDispatcher {
 public:
  // Registers `handler` (which decodes the response payload and invokes the
  // caller's callback) and returns the request_id to stamp on the outgoing
  // Message. OnceCallback (HolePunch via Bindings' base:: rung) so a
  // generated Proxy_ can capture a move-only user callback.
  uint64_t RegisterPendingResponse(
      base::OnceCallback<bool(Message*)> handler) {
    uint64_t id = next_request_id_++;
    pending_.emplace(id, std::move(handler));
    return id;
  }

  // Call from the Proxy_'s MessageReceiver::Accept() override for every
  // incoming message carrying Message::kFlagIsResponse. Returns false (and
  // leaves the message unhandled) if request_id doesn't match a pending
  // call -- the caller should treat that as a protocol error.
  [[nodiscard]] bool DispatchResponse(Message* message) {
    auto it = pending_.find(message->request_id());
    if (it == pending_.end()) {
      return false;
    }
    auto handler = std::move(it->second);
    pending_.erase(it);
    return std::move(handler).Run(message);
  }

  bool has_pending_responses() const { return !pending_.empty(); }

 private:
  uint64_t next_request_id_ = 1;
  std::unordered_map<uint64_t, base::OnceCallback<bool(Message*)>> pending_;
};

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_RESPONSE_DISPATCHER_H_
