// SelfOwnedReceiver<Interface>: owns an Interface impl and a Receiver bound
// to it. When the peer disconnects, the object drops its self-reference and
// is destroyed (impl included). Matching Chromium's
// mojo/public/cpp/bindings/self_owned_receiver.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_SELF_OWNED_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_SELF_OWNED_RECEIVER_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "whp/base/executor.h"

#include <memory>
#include <utility>

namespace mojo {

template <typename Interface>
class SelfOwnedReceiver
    : public std::enable_shared_from_this<SelfOwnedReceiver<Interface>> {
 public:
  static std::weak_ptr<SelfOwnedReceiver> Create(
      std::unique_ptr<Interface> impl,
      PendingReceiver<Interface> pending_receiver) {
    auto owned = std::shared_ptr<SelfOwnedReceiver>(
        new SelfOwnedReceiver(std::move(impl)));
    owned->self_keep_ = owned;
    owned->receiver_.Bind(std::move(pending_receiver));
    owned->receiver_.set_disconnect_handler([keep = owned]() mutable {
      whp::Executor::Current().PostTask(
          whp::BindOnce([keep]() mutable { keep->Close(); }));
    });
    return owned;
  }

  void Close() {
    receiver_.reset();
    impl_.reset();
    self_keep_.reset();
  }

  Receiver<Interface>& receiver() { return receiver_; }

 private:
  explicit SelfOwnedReceiver(std::unique_ptr<Interface> impl)
      : impl_(std::move(impl)), receiver_(impl_.get()) {}

  std::unique_ptr<Interface> impl_;
  Receiver<Interface> receiver_;
  std::shared_ptr<SelfOwnedReceiver> self_keep_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_SELF_OWNED_RECEIVER_H_
