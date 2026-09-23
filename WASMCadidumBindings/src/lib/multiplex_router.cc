#include "mojo/public/cpp/bindings/lib/multiplex_router.h"

#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "whp/base/executor.h"

#include <string>
#include <utility>

namespace mojo {

// static
std::shared_ptr<MultiplexRouter> MultiplexRouter::Create(
    ScopedMessagePipeHandle pipe,
    bool set_namespace_bit) {
  return std::shared_ptr<MultiplexRouter>(
      new MultiplexRouter(std::move(pipe), set_namespace_bit));
}

MultiplexRouter::MultiplexRouter(ScopedMessagePipeHandle pipe,
                                  bool set_namespace_bit)
    : connector_(std::move(pipe)), namespace_bit_(set_namespace_bit) {
  connector_.set_incoming_receiver(this);
  connector_.set_connection_error_handler([this] { OnPipeError(); });
}

MultiplexRouter::~MultiplexRouter() = default;

void MultiplexRouter::AttachPrimaryClient(MessageReceiver* client) {
  primary_client_ = client;
}

void MultiplexRouter::DetachPrimaryClient() {
  primary_client_ = nullptr;
}

MultiplexRouter::Endpoint& MultiplexRouter::FindOrCreateEndpoint(
    InterfaceId id) {
  return endpoints_[id];
}

InterfaceId MultiplexRouter::MintInterfaceId() {
  uint32_t id = next_id_value_++;
  if (namespace_bit_) {
    id |= kInterfaceIdNamespaceMask;
  }
  return id;
}

MultiplexRouter::PendingAssociation
MultiplexRouter::CreatePairPendingAssociation() {
  InterfaceId id = MintInterfaceId();
  FindOrCreateEndpoint(id);
  PendingAssociation result;
  result.local = ScopedInterfaceEndpointHandle(shared_from_this(), id);
  result.remote_id = id;
  return result;
}

ScopedInterfaceEndpointHandle MultiplexRouter::CreateLocalEndpointHandle(
    InterfaceId id) {
  FindOrCreateEndpoint(id);
  return ScopedInterfaceEndpointHandle(shared_from_this(), id);
}

void MultiplexRouter::AttachEndpointClient(
    const ScopedInterfaceEndpointHandle& handle,
    MessageReceiver* client) {
  Endpoint& ep = FindOrCreateEndpoint(handle.id());
  ep.client = client;
  if (ep.queued_messages.empty() || !ep.client) {
    return;
  }
  std::vector<Message> queued = std::move(ep.queued_messages);
  ep.queued_messages.clear();
  for (auto& msg : queued) {
    if (!ep.client || !ep.client->Accept(&msg)) {
      connector_.RaiseError();
      return;
    }
  }
}

void MultiplexRouter::DetachEndpointClient(InterfaceId id) {
  auto it = endpoints_.find(id);
  if (it != endpoints_.end()) {
    it->second.client = nullptr;
  }
  MaybeEraseEndpoint(id);
}

void MultiplexRouter::RetainEndpoint(InterfaceId id) {
  FindOrCreateEndpoint(id).refs++;
}

void MultiplexRouter::DropEndpointHandle(InterfaceId id) {
  auto it = endpoints_.find(id);
  if (it == endpoints_.end()) {
    return;
  }
  if (it->second.refs > 0) {
    it->second.refs--;
  }
  MaybeEraseEndpoint(id);
}

void MultiplexRouter::MaybeEraseEndpoint(InterfaceId id) {
  auto it = endpoints_.find(id);
  if (it == endpoints_.end()) {
    return;
  }
  if (it->second.refs <= 0 && !it->second.client &&
      it->second.queued_messages.empty()) {
    endpoints_.erase(it);
  }
}

void MultiplexRouter::CloseEndpoint(InterfaceId id) {
  NotifyPeerEndpointClosed(id);
  auto it = endpoints_.find(id);
  if (it != endpoints_.end()) {
    it->second.client = nullptr;
  }
  DropEndpointHandle(id);
}

void MultiplexRouter::SetPeerClosedHandler(InterfaceId id,
                                            base::OnceClosure handler) {
  FindOrCreateEndpoint(id).peer_closed_handler = std::move(handler);
}

void MultiplexRouter::SetPeerClosedWithReasonHandler(
    InterfaceId id, ConnectionErrorWithReasonCallback handler) {
  FindOrCreateEndpoint(id).peer_closed_with_reason_handler = std::move(handler);
}

void MultiplexRouter::NotifyPeerEndpointClosed(InterfaceId id,
                                               uint32_t custom_reason,
                                               const std::string& description) {
  Message message(0, 0, kInvalidInterfaceId);
  internal::WriteScalar(&message, id);
  if (custom_reason != 0 || !description.empty()) {
    internal::WriteScalar(&message, custom_reason);
    internal::WriteString(&message, description);
  }
  (void)connector_.Accept(&message);
}

void MultiplexRouter::NotifyDisconnectReason(uint32_t custom_reason,
                                             const std::string& description) {
  NotifyPeerEndpointClosed(kPrimaryInterfaceId, custom_reason, description);
}

bool MultiplexRouter::HandlePipeControlMessage(Message* message) {
  static constexpr uint32_t kPipeControlFlushName = 1;
  if (message->name() == kPipeControlFlushName) {
    if (message->has_flag(Message::kFlagIsResponse)) {
      flush_done_ = true;
      return true;
    }
    Message pong(kPipeControlFlushName, Message::kFlagIsResponse,
                 kInvalidInterfaceId);
    pong.set_request_id(message->request_id());
    (void)connector_.Accept(&pong);
    return true;
  }
  size_t offset = 0;
  InterfaceId closed_id = kInvalidInterfaceId;
  if (!internal::ReadScalar(*message, &offset, &closed_id)) {
    return false;
  }
  uint32_t custom_reason = 0;
  std::string description;
  if (offset < message->payload_num_bytes()) {
    if (!internal::ReadScalar(*message, &offset, &custom_reason) ||
        !internal::ReadString(*message, &offset, &description)) {
      return false;
    }
  }
  if (IsPrimaryInterfaceId(closed_id)) {
    disconnect_custom_reason_ = custom_reason;
    disconnect_description_ = description;
  }
  auto it = endpoints_.find(closed_id);
  if (it != endpoints_.end()) {
    if (it->second.peer_closed_with_reason_handler) {
      std::move(it->second.peer_closed_with_reason_handler)
          .Run(custom_reason, description);
    } else if (it->second.peer_closed_handler) {
      std::move(it->second.peer_closed_handler).Run();
    }
  }
  return true;
}

void MultiplexRouter::OnPipeError() {
  if (connection_error_with_reason_handler_) {
    std::move(connection_error_with_reason_handler_)
        .Run(disconnect_custom_reason_, disconnect_description_);
  } else if (connection_error_handler_) {
    std::move(connection_error_handler_).Run();
  }
}

bool MultiplexRouter::SendMessage(Message* message) {
  return connector_.Accept(message);
}

void MultiplexRouter::set_connection_error_handler(
    base::OnceClosure handler) {
  connection_error_handler_ = std::move(handler);
}

void MultiplexRouter::set_connection_error_with_reason_handler(
    ConnectionErrorWithReasonCallback handler) {
  connection_error_with_reason_handler_ = std::move(handler);
}

void MultiplexRouter::StartReceiving() {
  connector_.StartReceiving();
}

ScopedMessagePipeHandle MultiplexRouter::UnbindPipe() {
  return connector_.PassMessagePipe();
}

void MultiplexRouter::FlushForTesting() {
  static constexpr uint32_t kPipeControlFlushName = 1;
  flush_done_ = false;
  if (!connector_.is_valid() || connector_.encountered_error()) {
    whp::Executor::Current().RunUntilIdle();
    return;
  }
  Message ping(kPipeControlFlushName, Message::kFlagExpectsResponse,
               kInvalidInterfaceId);
  (void)connector_.Accept(&ping);
  connector_.SyncWaitFor([this] {
    return flush_done_ || connector_.encountered_error();
  });
}

bool MultiplexRouter::Accept(Message* message) {
  InterfaceId id = message->interface_id();
  if (id == kInvalidInterfaceId) {
    return HandlePipeControlMessage(message);
  }
  if (IsPrimaryInterfaceId(id)) {
    if (primary_client_) {
      return primary_client_->Accept(message);
    }
    return true;
  }
  Endpoint& ep = FindOrCreateEndpoint(id);
  if (ep.client) {
    return ep.client->Accept(message);
  }
  ep.queued_messages.push_back(std::move(*message));
  return true;
}

}  // namespace mojo
