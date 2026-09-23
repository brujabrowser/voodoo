// mojo::PendingAssociatedRemote<Interface>: the associated-interface analog
// of PendingRemote<Interface>, matching Chromium's
// mojo/public/cpp/bindings/pending_associated_remote.h.
//
// Unlike a primary PendingRemote (which wraps a real, OS-transmissible pipe
// handle and so can be attached to a Message and physically travel to
// another process), a PendingAssociatedRemote's ScopedInterfaceEndpointHandle
// is pure in-process bookkeeping bound to one specific MultiplexRouter --
// it never itself crosses a message boundary. What crosses the wire, when
// generated/hand-written Proxy_ code serializes one as a method parameter,
// is just the two plain integers interface_id() and version(); the
// receiving side's Stub_ reconstructs a *new* PendingAssociatedRemote by
// calling its own router's CreateLocalEndpointHandle() with that id.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_REMOTE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_REMOTE_H_

#include "mojo/public/cpp/bindings/associated_group.h"

#include <cstdint>
#include <utility>

namespace mojo {

template <typename Interface>
class PendingAssociatedReceiver;

template <typename Interface>
class PendingAssociatedRemote {
 public:
  PendingAssociatedRemote() = default;
  PendingAssociatedRemote(ScopedInterfaceEndpointHandle handle,
                           uint32_t version)
      : handle_(std::move(handle)), version_(version) {}
  PendingAssociatedRemote(PendingAssociatedRemote&&) noexcept = default;
  PendingAssociatedRemote& operator=(PendingAssociatedRemote&&) noexcept =
      default;
  PendingAssociatedRemote(const PendingAssociatedRemote&) = delete;
  PendingAssociatedRemote& operator=(const PendingAssociatedRemote&) = delete;

  bool is_valid() const { return handle_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  void reset() {
    handle_.reset();
    version_ = 0;
  }

  [[nodiscard]] ScopedInterfaceEndpointHandle PassHandle() {
    return std::move(handle_);
  }
  const ScopedInterfaceEndpointHandle& handle() const { return handle_; }
  InterfaceId interface_id() const { return handle_.id(); }
  uint32_t version() const { return version_; }

  // Mints a fresh associated-interface pair on `group`'s connection: this
  // object ends up wrapping one end (for a Proxy_ to send through), the
  // returned PendingAssociatedReceiver wraps the other (for a Stub_/impl to
  // receive through). Defined in pending_associated_receiver.h.
  [[nodiscard]] PendingAssociatedReceiver<Interface>
  InitWithNewEndpointAndPassReceiver(AssociatedGroup& group);

 private:
  ScopedInterfaceEndpointHandle handle_;
  uint32_t version_ = 0;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_PENDING_ASSOCIATED_REMOTE_H_
