// ScopedInterfaceEndpointHandle / AssociatedGroup: the associated-interface
// analogues of a raw message pipe handle and "the connection it lives on",
// matching Chromium's mojo/public/cpp/bindings/associated_group.h. An
// associated interface has no message pipe of its own -- it is one
// InterfaceId multiplexed onto its parent connection's single physical
// pipe, routed by that connection's MultiplexRouter (lib/multiplex_router.h).
#ifndef MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_GROUP_H_
#define MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_GROUP_H_

#include "mojo/public/cpp/bindings/interface_id.h"

#include <memory>
#include <utility>

namespace mojo {

class MultiplexRouter;

// Move-only. Owns one associated-interface id on a MultiplexRouter; on
// destruction, tells that router the endpoint is being given up.
class ScopedInterfaceEndpointHandle {
 public:
  ScopedInterfaceEndpointHandle();
  ScopedInterfaceEndpointHandle(std::shared_ptr<MultiplexRouter> router,
                                 InterfaceId id);
  ~ScopedInterfaceEndpointHandle();

  ScopedInterfaceEndpointHandle(ScopedInterfaceEndpointHandle&& other) noexcept;
  ScopedInterfaceEndpointHandle& operator=(
      ScopedInterfaceEndpointHandle&& other) noexcept;
  ScopedInterfaceEndpointHandle(const ScopedInterfaceEndpointHandle&) = delete;
  ScopedInterfaceEndpointHandle& operator=(
      const ScopedInterfaceEndpointHandle&) = delete;

  bool is_valid() const { return router_ != nullptr; }
  explicit operator bool() const { return is_valid(); }

  InterfaceId id() const { return id_; }
  const std::shared_ptr<MultiplexRouter>& router() const { return router_; }

  void reset();

  // Gives up ownership *without* telling the router to detach -- for the
  // one legitimate case where letting go of this handle isn't closing the
  // endpoint: peeling the bare id off to serialize into an outgoing
  // message (see e.g. Echo::Proxy_::SetListener in examples/echo). The
  // endpoint keeps whatever client (if any) is already attached to it.
  InterfaceId ReleaseWithoutClosing();

 private:
  void CloseIfNecessary();

  std::shared_ptr<MultiplexRouter> router_;
  InterfaceId id_ = kInvalidInterfaceId;
};

// Cheap, copyable capability to mint new associated interface pairs on a
// connection. Obtained from Remote<T>::associated_group() /
// Receiver<T>::associated_group() and threaded through to whichever call
// needs to hand out an associated interface (e.g. a
// PendingAssociatedRemote<Listener> parameter).
class AssociatedGroup {
 public:
  AssociatedGroup() = default;
  explicit AssociatedGroup(std::shared_ptr<MultiplexRouter> router)
      : router_(std::move(router)) {}

  bool is_valid() const { return router_ != nullptr; }
  const std::shared_ptr<MultiplexRouter>& router() const { return router_; }

 private:
  std::shared_ptr<MultiplexRouter> router_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_GROUP_H_
