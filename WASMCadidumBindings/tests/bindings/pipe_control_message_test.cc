// Direct tests of MultiplexRouter's router-level pipe-control-message
// protocol (peer-endpoint-closed propagation), independent of any
// AssociatedReceiver/AssociatedRemote -- proves the raw primitive works on
// its own, the same way interface_control_messages_test.cc does for the
// interface-level protocol.
#include "test.h"

#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

#include <utility>

using namespace mojo;

namespace {

void Pump(int iterations = 4) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(pipe_control_closing_local_endpoint_notifies_peer_router) {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  auto router_a = MultiplexRouter::Create(std::move(a), false);
  auto router_b = MultiplexRouter::Create(std::move(b), true);
  router_a->StartReceiving();
  router_b->StartReceiving();

  MultiplexRouter::PendingAssociation pending =
      router_a->CreatePairPendingAssociation();
  ScopedInterfaceEndpointHandle handle_b =
      router_b->CreateLocalEndpointHandle(pending.remote_id);

  bool peer_closed = false;
  router_b->SetPeerClosedHandler(handle_b.id(), [&] { peer_closed = true; });

  // Destroying router_a's handle closes its endpoint, which should notify
  // router_b's peer_closed handler for the same id.
  { ScopedInterfaceEndpointHandle local = std::move(pending.local); }

  Pump();

  EXPECT(peer_closed);
}

TEST(pipe_control_endpoint_erased_when_last_handle_drops) {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  auto router_a = MultiplexRouter::Create(std::move(a), false);
  auto router_b = MultiplexRouter::Create(std::move(b), true);
  EXPECT_EQ(router_a->EndpointCountForTesting(), 0u);
  {
    MultiplexRouter::PendingAssociation pending =
        router_a->CreatePairPendingAssociation();
    EXPECT(router_a->EndpointCountForTesting() >= 1u);
    (void)pending;
  }
  EXPECT_EQ(router_a->EndpointCountForTesting(), 0u);
  (void)router_b;
}

TEST(pipe_control_handler_registered_after_notification_does_not_fire) {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  auto router_a = MultiplexRouter::Create(std::move(a), false);
  auto router_b = MultiplexRouter::Create(std::move(b), true);
  router_a->StartReceiving();
  router_b->StartReceiving();

  MultiplexRouter::PendingAssociation pending =
      router_a->CreatePairPendingAssociation();
  ScopedInterfaceEndpointHandle handle_b =
      router_b->CreateLocalEndpointHandle(pending.remote_id);

  { ScopedInterfaceEndpointHandle local = std::move(pending.local); }
  Pump();

  bool peer_closed = false;
  router_b->SetPeerClosedHandler(handle_b.id(), [&] { peer_closed = true; });
  Pump();

  EXPECT(!peer_closed);
}

TEST(pipe_control_message_targets_reserved_invalid_interface_id) {
  // Sanity check on the discriminator this whole mechanism relies on: a
  // pipe control message's interface_id() is kInvalidInterfaceId, never a
  // value any real endpoint (primary or associated) could ever have.
  EXPECT(!IsValidInterfaceId(kInvalidInterfaceId));
  EXPECT(kInvalidInterfaceId != kPrimaryInterfaceId);
}
