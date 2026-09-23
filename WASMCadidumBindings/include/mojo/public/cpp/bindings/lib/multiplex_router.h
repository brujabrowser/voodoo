// MultiplexRouter: the piece that lets more than one interface (a primary
// interface plus any number of associated ones) share a single physical
// message pipe. Matches the role of Chromium's
// mojo/public/cpp/bindings/lib/multiplex_router.h.
//
// Exactly one MultiplexRouter owns each end of a physical pipe -- both
// Remote<T>/Receiver<T> primary-interface objects and every
// AssociatedRemote<T>/AssociatedReceiver<T> minted from that connection
// share the one Connector underneath. Sending never needs to route (the
// target InterfaceId already rides in the message header the wire format
// gives us for free); only *receiving* needs the endpoints_ map to fan a
// message out to the right client.
//
// Pipe-control-message protocol: closing a ScopedInterfaceEndpointHandle
// (CloseEndpoint) also sends the peer's router a best-effort notification
// (NotifyPeerEndpointClosed) carrying the closed id, distinguished from
// ordinary per-interface traffic by targeting the reserved
// kInvalidInterfaceId rather than by Message::name() -- see
// HandlePipeControlMessage. AssociatedReceiver<T>/AssociatedRemote<T>'s
// set_disconnect_handler is built on top of this (SetPeerClosedHandler).
// This is a genuinely different mechanism from
// lib/interface_control_messages.h's QueryVersion/RequireVersion, which are
// per-*interface* (ordinal-based, Message::name()) rather than per-*pipe*.
//
// endpoints_ is refcounted per live ScopedInterfaceEndpointHandle. The
// last Drop/Close with no client and no queued messages erases the entry.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/associated_group.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/bindings/interface_id.h"
#include "mojo/public/cpp/bindings/message.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mojo {

using ConnectionErrorWithReasonCallback =
    base::OnceCallback<void(uint32_t custom_reason, const std::string& description)>;

class MultiplexRouter : public MessageReceiver,
                         public std::enable_shared_from_this<MultiplexRouter> {
 public:
  static std::shared_ptr<MultiplexRouter> Create(ScopedMessagePipeHandle pipe,
                                                   bool set_namespace_bit);

  ~MultiplexRouter() override;

  MultiplexRouter(const MultiplexRouter&) = delete;
  MultiplexRouter& operator=(const MultiplexRouter&) = delete;

  Connector& connector() { return connector_; }
  AssociatedGroup associated_group() {
    return AssociatedGroup(shared_from_this());
  }

  // The primary (id 0) interface's local implementation/proxy dispatch
  // target. Exactly one of these per router.
  void AttachPrimaryClient(MessageReceiver* client);
  void DetachPrimaryClient();

  struct PendingAssociation {
    ScopedInterfaceEndpointHandle local;
    InterfaceId remote_id = kInvalidInterfaceId;
  };

  // Mints a fresh associated-interface id, reserves an endpoint for it on
  // this router (`local`), and hands back the same id (`remote_id`) to be
  // serialized into an outgoing message parameter -- the peer, on
  // receiving it, calls CreateLocalEndpointHandle(remote_id) on *its own*
  // router to obtain the other end.
  PendingAssociation CreatePairPendingAssociation();
  ScopedInterfaceEndpointHandle CreateLocalEndpointHandle(InterfaceId id);

  void AttachEndpointClient(const ScopedInterfaceEndpointHandle& handle,
                             MessageReceiver* client);
  void DetachEndpointClient(InterfaceId id);
  void CloseEndpoint(InterfaceId id);

  // Registers `handler` to be invoked when this router receives a
  // pipe-control notification that the peer's endpoint for `id` has closed
  // (see NotifyPeerEndpointClosed). Replaces any previously registered
  // handler for `id`. Does not fire retroactively -- if the notification
  // already arrived before a handler was registered, this handler will not
  // be told about it (matches set_connection_error_handler's existing
  // no-catch-up behavior; see connector.cc).
  void SetPeerClosedHandler(InterfaceId id, base::OnceClosure handler);
  void SetPeerClosedWithReasonHandler(InterfaceId id,
                                      ConnectionErrorWithReasonCallback handler);

  // `message`'s interface_id() must already be set (kPrimaryInterfaceId or
  // an id obtained from this router). One Connector serves every endpoint.
  [[nodiscard]] bool SendMessage(Message* message);

  void set_connection_error_handler(base::OnceClosure handler);
  void set_connection_error_with_reason_handler(
      ConnectionErrorWithReasonCallback handler);
  void NotifyDisconnectReason(uint32_t custom_reason,
                              const std::string& description);
  void NotifyPeerEndpointClosed(InterfaceId id,
                                uint32_t custom_reason = 0,
                                const std::string& description = {});
  ScopedMessagePipeHandle UnbindPipe();
  void FlushForTesting();
  size_t EndpointCountForTesting() const { return endpoints_.size(); }
  void RetainEndpoint(InterfaceId id);
  void DropEndpointHandle(InterfaceId id);
  void StartReceiving();

  // MessageReceiver: connector_ calls this for every inbound message; fans
  // it out by interface_id() to the primary client or an associated one.
  [[nodiscard]] bool Accept(Message* message) override;

 private:
  MultiplexRouter(ScopedMessagePipeHandle pipe, bool set_namespace_bit);

  struct Endpoint {
    MessageReceiver* client = nullptr;
    std::vector<Message> queued_messages;
    base::OnceClosure peer_closed_handler;
    ConnectionErrorWithReasonCallback peer_closed_with_reason_handler;
    int refs = 0;
  };

  void MaybeEraseEndpoint(InterfaceId id);

  Endpoint& FindOrCreateEndpoint(InterfaceId id);
  InterfaceId MintInterfaceId();
  void OnPipeError();

  // Handles an inbound message whose interface_id() == kInvalidInterfaceId
  // -- currently just the peer-endpoint-closed notification above. Returns
  // false (a protocol error, same as any other malformed message) only if
  // the payload doesn't even parse.
  bool HandlePipeControlMessage(Message* message);

  Connector connector_;
  bool namespace_bit_;
  uint32_t next_id_value_ = 1;
  std::unordered_map<InterfaceId, Endpoint> endpoints_;
  MessageReceiver* primary_client_ = nullptr;
  base::OnceClosure connection_error_handler_;
  ConnectionErrorWithReasonCallback connection_error_with_reason_handler_;
  uint32_t disconnect_custom_reason_ = 0;
  std::string disconnect_description_;
  bool flush_done_ = false;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_
