// Hand-written stand-in for what a `.voodoom` compiler (WASMVoodooCompile,
// forthcoming) would generate for:
//
//   interface EchoListener {
//     OnEcho(string value);
//   };
//
//   interface Echo {
//     EchoString(string in) => (string out);
//     SetListener(pending_associated_remote<EchoListener> listener);
//   };
//
// Exercises every rung of WASMCadidumBindings: EchoString is a primary,
// response-bearing call (Remote<Echo>/Receiver<Echo>, ResponseDispatcher);
// SetListener hands the callee an associated interface
// (PendingAssociatedRemote<EchoListener>, minted via AssociatedGroup) that
// the callee then uses to push OnEcho notifications back
// (AssociatedRemote<EchoListener>/AssociatedReceiver<EchoListener>).
//
// Wire payloads are hand-rolled -- length-prefixed strings, a bare
// {interface_id, version} pair for the associated parameter -- rather than
// coming from a generic struct serializer (see message.h's header comment
// for why WASMCadidumBindings doesn't ship one). A future .voodoom compiler
// would emit exactly this Proxy_/Stub_ shape from an interface definition.
//
// Proxy_ is only forward-declared inside its interface and defined
// out-of-line below: it publicly inherits from that interface, which is
// still an incomplete type at the point of a nested inline definition.
#ifndef WCB_EXAMPLES_ECHO_ECHO_INTERFACE_H_
#define WCB_EXAMPLES_ECHO_ECHO_INTERFACE_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/response_dispatcher.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace echo {

inline void WriteString(mojo::Message* message, const std::string& s) {
  uint32_t len = static_cast<uint32_t>(s.size());
  message->WritePayload(&len, sizeof(len));
  if (!s.empty()) {
    message->WritePayload(s.data(), s.size());
  }
}

// Reads a length-prefixed string starting at byte `*offset` of the
// message's payload, advancing *offset past it. False if truncated.
inline bool ReadString(const mojo::Message& message,
                        size_t* offset,
                        std::string* out) {
  const uint8_t* payload = message.payload();
  const uint32_t avail = message.payload_num_bytes();
  if (*offset + sizeof(uint32_t) > avail) {
    return false;
  }
  uint32_t len;
  std::memcpy(&len, payload + *offset, sizeof(len));
  *offset += sizeof(len);
  if (*offset + len > avail) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(payload + *offset), len);
  *offset += len;
  return true;
}

class EchoListener {
 public:
  virtual ~EchoListener() = default;
  virtual void OnEcho(const std::string& value) = 0;

  static constexpr uint32_t kOnEchoName = 0;

  class Proxy_;  // defined below, once EchoListener is a complete type

  class Stub_ : public mojo::MessageReceiver {
   public:
    Stub_(EchoListener* impl,
          mojo::MultiplexRouter* router,
          mojo::InterfaceId id = mojo::kPrimaryInterfaceId)
        : impl_(impl), router_(router), id_(id) {}

    [[nodiscard]] bool Accept(mojo::Message* message) override {
      if (message->name() != kOnEchoName) {
        return false;
      }
      size_t offset = 0;
      std::string value;
      if (!ReadString(*message, &offset, &value)) {
        return false;
      }
      impl_->OnEcho(value);
      return true;
    }

   private:
    EchoListener* impl_;
    mojo::MultiplexRouter* router_;
    mojo::InterfaceId id_;
  };
};

class EchoListener::Proxy_ : public EchoListener, public mojo::MessageReceiver {
 public:
  explicit Proxy_(mojo::MultiplexRouter* router,
                   mojo::InterfaceId id = mojo::kPrimaryInterfaceId)
      : router_(router), id_(id) {}

  void OnEcho(const std::string& value) override {
    mojo::Message message(kOnEchoName, 0, id_);
    WriteString(&message, value);
    (void)router_->SendMessage(&message);
  }

  [[nodiscard]] bool Accept(mojo::Message*) override {
    return false;  // OnEcho has no response; nothing should arrive here.
  }

 private:
  mojo::MultiplexRouter* router_;
  mojo::InterfaceId id_;
};

class Echo {
 public:
  virtual ~Echo() = default;
  virtual void EchoString(const std::string& in,
                           base::OnceCallback<void(std::string)> callback) = 0;
  virtual void SetListener(
      mojo::PendingAssociatedRemote<EchoListener> listener) = 0;

  static constexpr uint32_t kEchoStringName = 0;
  static constexpr uint32_t kSetListenerName = 1;

  class Proxy_;  // defined below, once Echo is a complete type

  class Stub_ : public mojo::MessageReceiver {
   public:
    Stub_(Echo* impl,
          mojo::MultiplexRouter* router,
          mojo::InterfaceId id = mojo::kPrimaryInterfaceId)
        : impl_(impl), router_(router), id_(id) {}

    [[nodiscard]] bool Accept(mojo::Message* message) override {
      if (message->name() == kEchoStringName) {
        return AcceptEchoString(message);
      }
      if (message->name() == kSetListenerName) {
        return AcceptSetListener(message);
      }
      return false;
    }

   private:
    bool AcceptEchoString(mojo::Message* message) {
      size_t offset = 0;
      std::string in;
      if (!ReadString(*message, &offset, &in)) {
        return false;
      }
      const uint64_t request_id = message->request_id();
      mojo::MultiplexRouter* router = router_;
      const mojo::InterfaceId id = id_;
      impl_->EchoString(in, [router, id, request_id](std::string out) {
        mojo::Message response(Echo::kEchoStringName,
                                mojo::Message::kFlagIsResponse, id);
        response.set_request_id(request_id);
        WriteString(&response, out);
        (void)router->SendMessage(&response);
      });
      return true;
    }

    bool AcceptSetListener(mojo::Message* message) {
      size_t offset = 0;
      const uint8_t* payload = message->payload();
      if (offset + 2 * sizeof(uint32_t) > message->payload_num_bytes()) {
        return false;
      }
      uint32_t iid = 0;
      uint32_t version = 0;
      std::memcpy(&iid, payload + offset, sizeof(iid));
      offset += sizeof(iid);
      std::memcpy(&version, payload + offset, sizeof(version));

      mojo::PendingAssociatedRemote<EchoListener> listener(
          router_->CreateLocalEndpointHandle(iid), version);
      impl_->SetListener(std::move(listener));
      return true;
    }

    Echo* impl_;
    mojo::MultiplexRouter* router_;
    mojo::InterfaceId id_;
  };
};

class Echo::Proxy_ : public Echo, public mojo::MessageReceiver {
 public:
  explicit Proxy_(mojo::MultiplexRouter* router,
                   mojo::InterfaceId id = mojo::kPrimaryInterfaceId)
      : router_(router), id_(id) {}

  void EchoString(const std::string& in,
                   base::OnceCallback<void(std::string)> callback) override {
    uint64_t request_id = responses_.RegisterPendingResponse(
        [callback = std::move(callback)](mojo::Message* response) mutable {
          size_t offset = 0;
          std::string out;
          if (!ReadString(*response, &offset, &out)) {
            return false;
          }
          std::move(callback).Run(out);
          return true;
        });
    mojo::Message message(kEchoStringName, mojo::Message::kFlagExpectsResponse,
                           id_);
    message.set_request_id(request_id);
    WriteString(&message, in);
    (void)router_->SendMessage(&message);
  }

  void SetListener(
      mojo::PendingAssociatedRemote<EchoListener> listener) override {
    uint32_t version = listener.version();
    mojo::ScopedInterfaceEndpointHandle handle = listener.PassHandle();
    uint32_t iid = handle.ReleaseWithoutClosing();

    mojo::Message message(kSetListenerName, 0, id_);
    message.WritePayload(&iid, sizeof(iid));
    message.WritePayload(&version, sizeof(version));
    (void)router_->SendMessage(&message);
  }

  [[nodiscard]] bool Accept(mojo::Message* message) override {
    if (!message->has_flag(mojo::Message::kFlagIsResponse)) {
      return false;
    }
    return responses_.DispatchResponse(message);
  }

 private:
  mojo::MultiplexRouter* router_;
  mojo::InterfaceId id_;
  mojo::internal::ResponseDispatcher responses_;
};

}  // namespace echo

#endif  // WCB_EXAMPLES_ECHO_ECHO_INTERFACE_H_
