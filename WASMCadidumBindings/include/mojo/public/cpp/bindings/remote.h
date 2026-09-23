// mojo::Remote<Interface>: the caller-side handle to a bound interface,
// matching Chromium's mojo/public/cpp/bindings/remote.h.
//
// Generated-code contract (what a hand-written or .voodoom-emitted
// interface must provide to be usable as a Remote<Interface>):
//
//   class Interface {
//    public:
//     virtual ~Interface() = default;
//     virtual void Method(Args..., ResponseCallback) = 0;  // pure virtual
//
//     // Implements Interface by serializing each call into a mojo::Message
//     // and sending it via router_->SendMessage(); doubles as the
//     // MessageReceiver that the router hands responses back to.
//     class Proxy_ : public Interface, public mojo::MessageReceiver {
//      public:
//       explicit Proxy_(mojo::MultiplexRouter* router,
//                        mojo::InterfaceId id = mojo::kPrimaryInterfaceId);
//       bool Accept(mojo::Message* message) override;  // response dispatch
//     };
//   };
//
// The same Proxy_ also backs AssociatedRemote<Interface> (associated_remote.h)
// -- it's handed a non-primary id there instead.
//
// Remote<Interface> owns a MultiplexRouter (this is the *primary*
// interface on a fresh pipe -- namespace_bit false, see multiplex_router.h)
// and an Interface::Proxy_ attached to it as the primary client.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_REMOTE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_REMOTE_H_

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
class Remote {
 public:
  Remote() = default;
  Remote(Remote&& o) noexcept
      : router_(std::move(o.router_)),
        proxy_(std::move(o.proxy_)),
        disconnect_handler_(std::move(o.disconnect_handler_)),
        disconnect_with_reason_handler_(
            std::move(o.disconnect_with_reason_handler_)),
        chpt_(o.chpt_) {
    o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
  }
  Remote& operator=(Remote&& o) noexcept {
    if (this != &o) {
      reset();
      router_ = std::move(o.router_);
      proxy_ = std::move(o.proxy_);
      disconnect_handler_ = std::move(o.disconnect_handler_);
      disconnect_with_reason_handler_ =
          std::move(o.disconnect_with_reason_handler_);
      chpt_ = o.chpt_;
      o.chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    return *this;
  }
  Remote(const Remote&) = delete;
  Remote& operator=(const Remote&) = delete;
  ~Remote() { reset(); }

  bool is_bound() const { return router_ != nullptr; }
  explicit operator bool() const { return is_bound(); }

  void Bind(PendingRemote<Interface> pending_remote) {
    reset();
    if (!pending_remote.is_valid()) {
      return;
    }
    router_ = MultiplexRouter::Create(pending_remote.PassPipe(),
                                       /*set_namespace_bit=*/false);
    proxy_ = std::make_unique<typename Interface::Proxy_>(router_.get());
    router_->AttachPrimaryClient(proxy_.get());
    chpt_ = internal::NameObject(proxy_.get(),
                                 internal::InterfaceTypeKey<Interface>());
    if (disconnect_handler_) {
      router_->set_connection_error_handler(std::move(disconnect_handler_));
    }
    if (disconnect_with_reason_handler_) {
      router_->set_connection_error_with_reason_handler(
          std::move(disconnect_with_reason_handler_));
    }
    router_->StartReceiving();
  }

  [[nodiscard]] PendingReceiver<Interface> BindNewPipeAndPassReceiver() {
    PendingRemote<Interface> pending_remote;
    PendingReceiver<Interface> pending_receiver =
        pending_remote.InitWithNewPipeAndPassReceiver();
    Bind(std::move(pending_remote));
    return pending_receiver;
  }

  void reset() {
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    if (router_) {
      router_->DetachPrimaryClient();
    }
    proxy_.reset();
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

  [[nodiscard]] PendingRemote<Interface> Unbind() {
    if (!router_) {
      return PendingRemote<Interface>();
    }
    if (chpt_ != cppgc::internal::CppHeapPointerTable::kNullHandle) {
      internal::FreeObject(chpt_);
      chpt_ = cppgc::internal::CppHeapPointerTable::kNullHandle;
    }
    router_->DetachPrimaryClient();
    ScopedMessagePipeHandle pipe = router_->UnbindPipe();
    proxy_.reset();
    router_.reset();
    return PendingRemote<Interface>(std::move(pipe), 0);
  }

  void FlushForTesting() {
    if (router_) {
      router_->FlushForTesting();
    }
  }

  void PauseReceiverUntilFlushCompletes() {
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

  Interface* get() const { return proxy_.get(); }
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  // The concrete Proxy_, for callers that need something beyond
  // Interface's own pure-virtual surface -- namely a `[Sync]` .voodoom
  // method's extra blocking overload, which only exists on Proxy_ (Stub_/
  // the impl side never needs it, so it was never a good fit for
  // Interface's own pure-virtual list; see cpp_generator.cc's
  // EmitSyncProxyMethod). `remote->Method(...)` still reaches the ordinary
  // async, polymorphic-through-Interface* call; `remote.proxy()->Method(
  // ...)` is how to reach a sync one.
  typename Interface::Proxy_* proxy() const { return proxy_.get(); }

  // CHPT handle for this bound Proxy_. Get fails unless the interned
  // tag matches Interface.
  v8::CppHeapPointerHandle chpt_handle() const { return chpt_; }
  static Interface* FromHandle(v8::CppHeapPointerHandle h) {
    return static_cast<Interface*>(
        internal::GetObject(h, internal::InterfaceTypeKey<Interface>()));
  }

 private:
  std::shared_ptr<MultiplexRouter> router_;
  std::unique_ptr<typename Interface::Proxy_> proxy_;
  base::OnceClosure disconnect_handler_;
  ConnectionErrorWithReasonCallback disconnect_with_reason_handler_;
  v8::CppHeapPointerHandle chpt_ =
      cppgc::internal::CppHeapPointerTable::kNullHandle;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_REMOTE_H_
