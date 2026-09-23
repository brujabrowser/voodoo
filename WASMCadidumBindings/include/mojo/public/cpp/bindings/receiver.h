// mojo::Receiver<Interface>: binds a local Interface implementation to an
// endpoint and dispatches incoming calls to it, matching Chromium's
// mojo/public/cpp/bindings/receiver.h.
//
// Generated-code contract (see remote.h for Proxy_'s half):
//
//   class Interface::Stub_ : public mojo::MessageReceiver {
//    public:
//     Stub_(Interface* impl, mojo::MultiplexRouter* router,
//           mojo::InterfaceId id = mojo::kPrimaryInterfaceId);
//     bool Accept(mojo::Message* message) override;  // decode + dispatch,
//         // sending a response via router_->SendMessage() if the method
//         // has a response callback.
//   };
//
// The same Stub_ also backs AssociatedReceiver<Interface>
// (associated_receiver.h) -- it's handed a non-primary id there instead.
//
// Receiver<Interface> owns a MultiplexRouter (namespace_bit true -- the
// opposite of Remote<Interface>'s, since a Remote/Receiver pair always sits
// on opposite ends of one pipe) and an Interface::Stub_ attached as the
// primary client.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_group.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/type_intern.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

#include <memory>
#include <string>
#include <utility>

namespace mojo {

template <typename Interface>
class Receiver {
 public:
  Receiver() = default;
  explicit Receiver(Interface* impl) : impl_(impl) {}
  Receiver(Receiver&& o) noexcept
      : impl_(o.impl_),
        router_(std::move(o.router_)),
        stub_(std::move(o.stub_)),
        disconnect_handler_(std::move(o.disconnect_handler_)),
        disconnect_with_reason_handler_(
            std::move(o.disconnect_with_reason_handler_)),
        chpt_(o.chpt_) {
    o.impl_ = nullptr;
    o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
  }
  Receiver& operator=(Receiver&& o) noexcept {
    if (this != &o) {
      reset();
      impl_ = o.impl_;
      router_ = std::move(o.router_);
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
  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;
  ~Receiver() { reset(); }

  bool is_bound() const { return router_ != nullptr; }
  explicit operator bool() const { return is_bound(); }

  void Bind(PendingReceiver<Interface> pending_receiver) {
    reset();
    if (!pending_receiver.is_valid()) {
      return;
    }
    router_ = MultiplexRouter::Create(pending_receiver.PassPipe(),
                                       /*set_namespace_bit=*/true);
    stub_ = std::make_unique<typename Interface::Stub_>(impl_, router_.get());
    router_->AttachPrimaryClient(stub_.get());
    if (impl_) {
      chpt_ = internal::NameObject(impl_,
                                   internal::InterfaceTypeKey<Interface>());
    }
    if (disconnect_handler_) {
      router_->set_connection_error_handler(std::move(disconnect_handler_));
    }
    if (disconnect_with_reason_handler_) {
      router_->set_connection_error_with_reason_handler(
          std::move(disconnect_with_reason_handler_));
    }
    router_->StartReceiving();
  }

  [[nodiscard]] PendingRemote<Interface> BindNewPipeAndPassRemote() {
    PendingReceiver<Interface> pending_receiver;
    PendingRemote<Interface> pending_remote =
        pending_receiver.InitWithNewPipeAndPassRemote();
    Bind(std::move(pending_receiver));
    return pending_remote;
  }

  void reset() {
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    if (router_) {
      router_->DetachPrimaryClient();
    }
    stub_.reset();
    router_.reset();
  }

  void set_disconnect_handler(base::OnceClosure handler) {
    if (router_) {
      router_->set_connection_error_handler(std::move(handler));
    } else {
      disconnect_handler_ = std::move(handler);
    }
  }

  void set_disconnect_with_reason_handler(
      ConnectionErrorWithReasonCallback handler) {
    if (router_) {
      router_->set_connection_error_with_reason_handler(std::move(handler));
    } else {
      disconnect_with_reason_handler_ = std::move(handler);
    }
  }

  void reset_with_reason(uint32_t custom_reason, const std::string& description) {
    if (router_) {
      router_->NotifyDisconnectReason(custom_reason, description);
    }
    reset();
  }

  [[nodiscard]] PendingReceiver<Interface> Unbind() {
    if (!router_) {
      return PendingReceiver<Interface>();
    }
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    router_->DetachPrimaryClient();
    ScopedMessagePipeHandle pipe = router_->UnbindPipe();
    stub_.reset();
    router_.reset();
    return PendingReceiver<Interface>(std::move(pipe));
  }

  void FlushForTesting() {
    if (router_) {
      router_->FlushForTesting();
    }
  }

  void PauseIncomingMethodCallProcessing() {
    if (router_) {
      router_->connector().PauseIncomingMethodCallProcessing();
    }
  }

  void ResumeIncomingMethodCallProcessing() {
    if (router_) {
      router_->connector().ResumeIncomingMethodCallProcessing();
    }
  }

  AssociatedGroup associated_group() const {
    return router_ ? router_->associated_group() : AssociatedGroup();
  }

  v8::CppHeapPointerHandle chpt_handle() const { return chpt_; }
  static Interface* FromHandle(v8::CppHeapPointerHandle h) {
    return static_cast<Interface*>(
        internal::GetObject(h, internal::InterfaceTypeKey<Interface>()));
  }

 private:
  Interface* impl_ = nullptr;
  std::shared_ptr<MultiplexRouter> router_;
  std::unique_ptr<typename Interface::Stub_> stub_;
  base::OnceClosure disconnect_handler_;
  ConnectionErrorWithReasonCallback disconnect_with_reason_handler_;
  v8::CppHeapPointerHandle chpt_ =
      cppgc::internal::CppHeapPointerTable::kNullHandle;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_H_
