// mojo::AssociatedReceiver<Interface>: binds a local Interface
// implementation to an associated-interface endpoint, matching Chromium's
// mojo/public/cpp/bindings/associated_receiver.h. Reuses the exact same
// Interface::Stub_ contract as Receiver<Interface> (see receiver.h).
#ifndef MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_RECEIVER_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/type_intern.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"

#include <memory>
#include <string>
#include <utility>

namespace mojo {

template <typename Interface>
class AssociatedReceiver {
 public:
  AssociatedReceiver() = default;
  explicit AssociatedReceiver(Interface* impl) : impl_(impl) {}
  AssociatedReceiver(AssociatedReceiver&& o) noexcept
      : impl_(o.impl_),
        router_(std::move(o.router_)),
        handle_(std::move(o.handle_)),
        stub_(std::move(o.stub_)),
        disconnect_handler_(std::move(o.disconnect_handler_)),
        disconnect_with_reason_handler_(
            std::move(o.disconnect_with_reason_handler_)),
        chpt_(o.chpt_) {
    o.impl_ = nullptr;
    o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
  }
  AssociatedReceiver& operator=(AssociatedReceiver&& o) noexcept {
    if (this != &o) {
      reset();
      impl_ = o.impl_;
      router_ = std::move(o.router_);
      handle_ = std::move(o.handle_);
      stub_ = std::move(o.stub_);
      disconnect_handler_ = std::move(o.disconnect_handler_);
      disconnect_with_reason_handler_ =
          std::move(o.disconnect_with_reason_handler_);
      chpt_ = o.chpt_;
      o.impl_ = nullptr;
      o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    return *this;
  }
  AssociatedReceiver(const AssociatedReceiver&) = delete;
  AssociatedReceiver& operator=(const AssociatedReceiver&) = delete;
  ~AssociatedReceiver() { reset(); }

  bool is_bound() const { return stub_ != nullptr; }
  explicit operator bool() const { return is_bound(); }

  void Bind(PendingAssociatedReceiver<Interface> pending_receiver) {
    reset();
    if (!pending_receiver.is_valid()) {
      return;
    }
    handle_ = pending_receiver.PassHandle();
    router_ = handle_.router();
    stub_ = std::make_unique<typename Interface::Stub_>(impl_, router_.get(),
                                                          handle_.id());
    router_->AttachEndpointClient(handle_, stub_.get());
    if (impl_) {
      chpt_ = internal::NameObject(impl_,
                                   internal::InterfaceTypeKey<Interface>());
    }
    if (disconnect_handler_) {
      router_->SetPeerClosedHandler(handle_.id(),
                                    std::move(disconnect_handler_));
    }
    if (disconnect_with_reason_handler_) {
      router_->SetPeerClosedWithReasonHandler(
          handle_.id(), std::move(disconnect_with_reason_handler_));
    }
  }

  // Invoked when the peer's PendingAssociatedRemote/AssociatedRemote for
  // this same endpoint goes away (see MultiplexRouter::SetPeerClosedHandler
  // in lib/multiplex_router.h). Unlike Receiver<T>'s disconnect handler,
  // this is per-endpoint, not per-pipe -- the underlying physical
  // connection can still be perfectly healthy.
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
    stub_.reset();
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
    stub_.reset();
    handle_.reset();
    router_.reset();
  }

  v8::CppHeapPointerHandle chpt_handle() const { return chpt_; }
  static Interface* FromHandle(v8::CppHeapPointerHandle h) {
    return static_cast<Interface*>(
        internal::GetObject(h, internal::InterfaceTypeKey<Interface>()));
  }

 private:
  Interface* impl_ = nullptr;
  std::shared_ptr<MultiplexRouter> router_;
  ScopedInterfaceEndpointHandle handle_;
  std::unique_ptr<typename Interface::Stub_> stub_;
  base::OnceClosure disconnect_handler_;
  ConnectionErrorWithReasonCallback disconnect_with_reason_handler_;
  v8::CppHeapPointerHandle chpt_ =
      cppgc::internal::CppHeapPointerTable::kNullHandle;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_ASSOCIATED_RECEIVER_H_
