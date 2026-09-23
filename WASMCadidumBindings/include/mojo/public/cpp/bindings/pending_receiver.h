// mojo::PendingReceiver<Interface>: an unbound message pipe endpoint
// destined to become a Receiver<Interface>, matching Chromium's
// mojo/public/cpp/bindings/pending_receiver.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_PENDING_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_PENDING_RECEIVER_H_

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/system/message_pipe.h"

#include <cstdint>
#include <utility>

namespace mojo {

template <typename Interface>
class PendingReceiver {
 public:
  PendingReceiver() = default;
  explicit PendingReceiver(ScopedMessagePipeHandle pipe)
      : pipe_(std::move(pipe)) {}
  PendingReceiver(PendingReceiver&&) noexcept = default;
  PendingReceiver& operator=(PendingReceiver&&) noexcept = default;
  PendingReceiver(const PendingReceiver&) = delete;
  PendingReceiver& operator=(const PendingReceiver&) = delete;

  bool is_valid() const { return pipe_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  void reset() { pipe_.reset(); }

  [[nodiscard]] ScopedMessagePipeHandle PassPipe() { return std::move(pipe_); }
  const ScopedMessagePipeHandle& pipe() const { return pipe_; }

  // Mirror image of PendingRemote<Interface>::InitWithNewPipeAndPassReceiver().
  [[nodiscard]] PendingRemote<Interface> InitWithNewPipeAndPassRemote();

 private:
  ScopedMessagePipeHandle pipe_;
};

template <typename Interface>
PendingReceiver<Interface>
PendingRemote<Interface>::InitWithNewPipeAndPassReceiver() {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  pipe_ = std::move(a);
  return PendingReceiver<Interface>(std::move(b));
}

template <typename Interface>
PendingRemote<Interface>
PendingReceiver<Interface>::InitWithNewPipeAndPassRemote() {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  pipe_ = std::move(a);
  return PendingRemote<Interface>(std::move(b), 0);
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_PENDING_RECEIVER_H_
