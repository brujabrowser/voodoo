// mojo::PendingRemote<Interface>: an unbound message pipe endpoint destined
// to become a Remote<Interface>, matching Chromium's
// mojo/public/cpp/bindings/pending_remote.h. `Interface` is never actually
// instantiated here -- it's a phantom type tag that keeps, say, a
// PendingRemote<Echo> from being handed to a Receiver<EchoListener> by
// accident. The interface itself (its Proxy_/Stub_) is supplied by
// hand-written or .voodoom-generated code that includes this runtime.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_PENDING_REMOTE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_PENDING_REMOTE_H_

#include "mojo/public/cpp/system/message_pipe.h"

#include <cstdint>
#include <utility>

namespace mojo {

template <typename Interface>
class PendingReceiver;

template <typename Interface>
class PendingRemote {
 public:
  PendingRemote() = default;
  PendingRemote(ScopedMessagePipeHandle pipe, uint32_t version)
      : pipe_(std::move(pipe)), version_(version) {}
  PendingRemote(PendingRemote&&) noexcept = default;
  PendingRemote& operator=(PendingRemote&&) noexcept = default;
  PendingRemote(const PendingRemote&) = delete;
  PendingRemote& operator=(const PendingRemote&) = delete;

  bool is_valid() const { return pipe_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  void reset() {
    pipe_.reset();
    version_ = 0;
  }

  [[nodiscard]] ScopedMessagePipeHandle PassPipe() { return std::move(pipe_); }
  const ScopedMessagePipeHandle& pipe() const { return pipe_; }

  uint32_t version() const { return version_; }

  // Mirror image of Remote<Interface>::BindNewPipeAndPassReceiver(): creates
  // a fresh pipe, keeps this end, hands back a PendingReceiver for the
  // other. Defined in pending_receiver.h (needs its full definition).
  [[nodiscard]] PendingReceiver<Interface> InitWithNewPipeAndPassReceiver();

 private:
  ScopedMessagePipeHandle pipe_;
  uint32_t version_ = 0;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_PENDING_REMOTE_H_
