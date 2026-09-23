#include "test.h"

#include "echo_interface.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

namespace {

class ListenerImpl : public echo::EchoListener {
 public:
  void OnEcho(const std::string& value) override { seen.push_back(value); }
  std::vector<std::string> seen;
};

class EchoImpl : public echo::Echo {
 public:
  void EchoString(const std::string& in,
                   base::OnceCallback<void(std::string)> callback) override {
    callback(in);
    if (listener_.is_bound()) {
      listener_->OnEcho("pushed:" + in);
    }
  }
  void SetListener(
      mojo::PendingAssociatedRemote<echo::EchoListener> listener) override {
    listener_.Bind(std::move(listener));
  }

  bool listener_bound() const { return listener_.is_bound(); }

 private:
  mojo::AssociatedRemote<echo::EchoListener> listener_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(associated_interface_end_to_end) {
  EchoImpl echo_impl;
  mojo::Receiver<echo::Echo> echo_receiver(&echo_impl);
  mojo::Remote<echo::Echo> echo_remote;
  echo_receiver.Bind(echo_remote.BindNewPipeAndPassReceiver());

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);

  mojo::AssociatedGroup group = echo_remote.associated_group();
  EXPECT(group.is_valid());

  mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote;
  mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver =
      listener_remote.InitWithNewEndpointAndPassReceiver(group);
  EXPECT(listener_remote.is_valid());
  EXPECT(listener_receiver.is_valid());

  echo_remote->SetListener(std::move(listener_remote));
  listener_receiver_obj.Bind(std::move(listener_receiver));
  EXPECT(listener_receiver_obj.is_bound());

  Pump();
  EXPECT(echo_impl.listener_bound());

  bool responded = false;
  echo_remote->EchoString("assoc", [&responded](std::string) {
    responded = true;
  });

  Pump();

  EXPECT(responded);
  EXPECT_EQ(listener_impl.seen.size(), 1u);
  if (!listener_impl.seen.empty()) {
    EXPECT_EQ(listener_impl.seen[0], "pushed:assoc");
  }
}

TEST(associated_ids_use_disjoint_namespaces) {
  // Remote<T>::BindNewPipeAndPassReceiver() gives the Remote-side router
  // namespace_bit=false and the Receiver-side router namespace_bit=true
  // (see remote.h/receiver.h) -- minted associated ids from either side
  // must never collide, which we can observe via the top bit.
  EchoImpl echo_impl;
  mojo::Receiver<echo::Echo> echo_receiver(&echo_impl);
  mojo::Remote<echo::Echo> echo_remote;
  echo_receiver.Bind(echo_remote.BindNewPipeAndPassReceiver());

  mojo::AssociatedGroup remote_group = echo_remote.associated_group();

  mojo::PendingAssociatedRemote<echo::EchoListener> pr1;
  mojo::PendingAssociatedReceiver<echo::EchoListener> recv1 =
      pr1.InitWithNewEndpointAndPassReceiver(remote_group);
  mojo::PendingAssociatedRemote<echo::EchoListener> pr2;
  mojo::PendingAssociatedReceiver<echo::EchoListener> recv2 =
      pr2.InitWithNewEndpointAndPassReceiver(remote_group);

  EXPECT((recv1.interface_id() & mojo::kInterfaceIdNamespaceMask) == 0);
  EXPECT((recv2.interface_id() & mojo::kInterfaceIdNamespaceMask) == 0);
  EXPECT(recv1.interface_id() != recv2.interface_id());
}

// The two tests below build an associated pair across two genuinely
// separate, pipe-connected MultiplexRouters -- router_a mints the pair
// (CreatePairPendingAssociation), router_b reconstructs the other end
// (CreateLocalEndpointHandle) exactly the way a generated Stub_ would when
// deserializing a PendingAssociatedRemote/PendingAssociatedReceiver
// parameter -- so a set_disconnect_handler firing here proves the
// notification actually crossed the physical pipe, not just a shared
// in-process router.

TEST(associated_receiver_disconnect_handler_fires_when_remote_resets) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  auto router_a = mojo::MultiplexRouter::Create(std::move(a), false);
  auto router_b = mojo::MultiplexRouter::Create(std::move(b), true);
  router_a->StartReceiving();
  router_b->StartReceiving();

  mojo::MultiplexRouter::PendingAssociation pending =
      router_a->CreatePairPendingAssociation();
  mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver(
      std::move(pending.local));
  mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote(
      router_b->CreateLocalEndpointHandle(pending.remote_id), 0);

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);
  bool receiver_saw_peer_closed = false;
  listener_receiver_obj.set_disconnect_handler(
      [&] { receiver_saw_peer_closed = true; });
  listener_receiver_obj.Bind(std::move(listener_receiver));

  mojo::AssociatedRemote<echo::EchoListener> listener_remote_obj;
  listener_remote_obj.Bind(std::move(listener_remote));
  Pump();
  EXPECT(!receiver_saw_peer_closed);

  listener_remote_obj.reset();
  Pump();

  EXPECT(receiver_saw_peer_closed);
}

TEST(associated_remote_disconnect_handler_fires_when_receiver_resets) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  auto router_a = mojo::MultiplexRouter::Create(std::move(a), false);
  auto router_b = mojo::MultiplexRouter::Create(std::move(b), true);
  router_a->StartReceiving();
  router_b->StartReceiving();

  mojo::MultiplexRouter::PendingAssociation pending =
      router_a->CreatePairPendingAssociation();
  mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver(
      std::move(pending.local));
  mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote(
      router_b->CreateLocalEndpointHandle(pending.remote_id), 0);

  mojo::AssociatedRemote<echo::EchoListener> listener_remote_obj;
  bool remote_saw_peer_closed = false;
  listener_remote_obj.set_disconnect_handler(
      [&] { remote_saw_peer_closed = true; });
  listener_remote_obj.Bind(std::move(listener_remote));

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);
  listener_receiver_obj.Bind(std::move(listener_receiver));
  Pump();
  EXPECT(!remote_saw_peer_closed);

  listener_receiver_obj.reset();
  Pump();

  EXPECT(remote_saw_peer_closed);
}
