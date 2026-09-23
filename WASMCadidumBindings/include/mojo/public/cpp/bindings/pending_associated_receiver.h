// mojo::PendingAssociatedReceiver<Interface>: the associated-interface
// analog of PendingReceiver<Interface>, matching Chromium's
// mojo/public/cpp/bindings/pending_associated_receiver.h. See
// pending_associated_remote.h for why this wraps a real, but purely
// in-process, ScopedInterfaceEndpointHandle.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_RECEIVER_H_

#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"

#include <utility>

namespace mojo {

template <typename Interface>
class PendingAssociatedReceiver {
 public:
  PendingAssociatedReceiver() = default;
  explicit PendingAssociatedReceiver(ScopedInterfaceEndpointHandle handle)
      : handle_(std::move(handle)) {}
  PendingAssociatedReceiver(PendingAssociatedReceiver&&) noexcept = default;
  PendingAssociatedReceiver& operator=(PendingAssociatedReceiver&&) noexcept =
      default;
  PendingAssociatedReceiver(const PendingAssociatedReceiver&) = delete;
  PendingAssociatedReceiver& operator=(const PendingAssociatedReceiver&) =
      delete;

  bool is_valid() const { return handle_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  void reset() { handle_.reset(); }

  [[nodiscard]] ScopedInterfaceEndpointHandle PassHandle() {
    return std::move(handle_);
  }
  const ScopedInterfaceEndpointHandle& handle() const { return handle_; }
  InterfaceId interface_id() const { return handle_.id(); }

  [[nodiscard]] PendingAssociatedRemote<Interface>
  InitWithNewEndpointAndPassRemote(AssociatedGroup& group);

 private:
  ScopedInterfaceEndpointHandle handle_;
};

template <typename Interface>
PendingAssociatedReceiver<Interface>
PendingAssociatedRemote<Interface>::InitWithNewEndpointAndPassReceiver(
    AssociatedGroup& group) {
  MultiplexRouter::PendingAssociation pending =
      group.router()->CreatePairPendingAssociation();
  handle_ = ScopedInterfaceEndpointHandle(group.router(), pending.remote_id);
  version_ = 0;
  return PendingAssociatedReceiver<Interface>(std::move(pending.local));
}

template <typename Interface>
PendingAssociatedRemote<Interface>
PendingAssociatedReceiver<Interface>::InitWithNewEndpointAndPassRemote(
    AssociatedGroup& group) {
  MultiplexRouter::PendingAssociation pending =
      group.router()->CreatePairPendingAssociation();
  handle_ = std::move(pending.local);
  return PendingAssociatedRemote<Interface>(
      ScopedInterfaceEndpointHandle(group.router(), pending.remote_id), 0);
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_RECEIVER_H_
