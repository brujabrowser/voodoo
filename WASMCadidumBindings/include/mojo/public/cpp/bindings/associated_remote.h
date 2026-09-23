// mojo::AssociatedRemote<Interface>: the caller-side handle to an
// associated interface, matching Chromium's
// mojo/public/cpp/bindings/associated_remote.h. Reuses the exact same
// Interface::Proxy_ contract as Remote<Interface> (see remote.h) -- the
// only difference is which InterfaceId gets passed to it and to the
// router: kPrimaryInterfaceId for Remote, whatever
// PendingAssociatedRemote::interface_id() carries for this.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_REMOTE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_REMOTE_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/type_intern.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"

#include <memory>
#include <string>
#include <utility>

namespace mojo {

template <typename Interface>
class AssociatedRemote {
 public:
  AssociatedRemote() = default;
  AssociatedRemote(AssociatedRemote&& o) noexcept
      : router_(std::move(o.router_)),
        handle_(std::move(o.handle_)),
        proxy_(std::move(o.proxy_)),
        disconnect_handler_(std::move(o.disconnect_handler_)),
        disconnect_with_reason_handler_(
            std::move(o.disconnect_with_reason_handler_)),
        chpt_(o.chpt_) {
    o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
  }
  AssociatedRemote& operator=(AssociatedRemote&& o) noexcept {
    if (this != &o) {
      reset();
      router_ = std::move(o.router_);
      handle_ = std::move(o.handle_);
      proxy_ = std::move(o.proxy_);
      disconnect_handler_ = std::move(o.disconnect_handler_);
      disconnect_with_reason_handler_ =
          std::move(o.disconnect_with_reason_handler_);
      chpt_ = o.chpt_;
      o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    return *this;
  }
  AssociatedRemote(const AssociatedRemote&) = delete;
  AssociatedRemote& operator=(const AssociatedRemote&) = delete;
  ~AssociatedRemote() { reset(); }

  bool is_bound() const { return proxy_ != nullptr; }
  explicit operator bool() const { return is_bound(); }

  void Bind(PendingAssociatedRemote<Interface> pending_remote) {
    reset();
    if (!pending_remote.is_valid()) {
      return;
    }
    handle_ = pending_remote.PassHandle();
    router_ = handle_.router();
    proxy_ =
        std::make_unique<typename Interface::Proxy_>(router_.get(), handle_.id());
    router_->AttachEndpointClient(handle_, proxy_.get());
    chpt_ = internal::NameObject(proxy_.get(),
                                 internal::InterfaceTypeKey<Interface>());
    if (disconnect_handler_) {
      router_->SetPeerClosedHandler(handle_.id(),
                                    std::move(disconnect_handler_));
    }
    if (disconnect_with_reason_handler_) {
      router_->SetPeerClosedWithReasonHandler(
          handle_.id(), std::move(disconnect_with_reason_handler_));
    }
  }

  // See AssociatedReceiver<Interface>::set_disconnect_handler -- same
  // per-endpoint (not per-pipe) peer-closed notification, just observed
  // from the remote side instead of the receiver side.
  void set_disconnect_handler(base::OnceClosure handler) {
    if (router_) {
      router_->SetPeerClosedHandler(handle_.id(), std::move(handler));
    } else {
      disconnect_handler_ = std::move(handler);
    }
  }

  void set_disconnect_with_reason_handler(
      ConnectionErrorWithReasonCallback handler) {
    if (router_) {
      router_->SetPeerClosedWithReasonHandler(handle_.id(), std::move(handler));
    } else {
      disconnect_with_reason_handler_ = std::move(handler);
    }
  }

  void reset_with_reason(uint32_t custom_reason, const std::string& description) {
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    if (router_) {
      router_->NotifyPeerEndpointClosed(handle_.id(), custom_reason,
                                        description);
      router_->DetachEndpointClient(handle_.id());
    }
    handle_.ReleaseWithoutClosing();
    proxy_.reset();
    router_.reset();
  }

  void reset() {
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    if (router_) {
      router_->DetachEndpointClient(handle_.id());
    }
    proxy_.reset();
    handle_.reset();
    router_.reset();
  }

  Interface* get() const { return proxy_.get(); }
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  // See Remote<Interface>::proxy() (remote.h) -- same reasoning, same
  // `[Sync]` use case, just for an associated interface's Proxy_ instead
  // of a primary one.
  typename Interface::Proxy_* proxy() const { return proxy_.get(); }

  v8::CppHeapPointerHandle chpt_handle() const { return chpt_; }
  static Interface* FromHandle(v8::CppHeapPointerHandle h) {
    return static_cast<Interface*>(
        internal::GetObject(h, internal::InterfaceTypeKey<Interface>()));
  }

 private:
  std::shared_ptr<MultiplexRouter> router_;
  ScopedInterfaceEndpointHandle handle_;
  std::unique_ptr<typename Interface::Proxy_> proxy_;
  base::OnceClosure disconnect_handler_;
  ConnectionErrorWithReasonCallback disconnect_with_reason_handler_;
  v8::CppHeapPointerHandle chpt_ =
      cppgc::internal::CppHeapPointerTable::kNullHandle;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_REMOTE_H_
