// Proves voodoomc's output isn't just structurally similar to
// WASMCadidumBindings/examples/echo/echo_interface.h -- it's compiled here
// straight from the generated header (echo_interface_gen.h, produced from
// examples/echo/echo.voodoom by the custom command in CMakeLists.txt) and
// run through the same request/response + associated-interface roundtrip
// as that repo's own tests/bindings/remote_receiver_roundtrip.cc and
// associated_roundtrip.cc.
#include "test.h"

#include "echo_interface_gen.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "src/sandbox/external-pointer-table.h"
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
    ++call_count;
    last_in = in;
    callback(in + in);
    if (listener_.is_bound()) {
      listener_->OnEcho("pushed:" + in);
    }
  }
  void SetListener(
      mojo::PendingAssociatedRemote<echo::EchoListener> listener) override {
    listener_.Bind(std::move(listener));
  }

  bool listener_bound() const { return listener_.is_bound(); }
  void ResetListener() { listener_.reset(); }

  int call_count = 0;
  std::string last_in;

 private:
  mojo::AssociatedRemote<echo::EchoListener> listener_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_type_key_intern_and_chpt_rejects_wrong_interface) {
  EXPECT_EQ(std::string(echo::Echo::intern_key()),
            std::string("interface:echo.Echo"));
  EXPECT_EQ(std::string(echo::EchoListener::intern_key()),
            std::string("interface:echo.EchoListener"));
  EXPECT(echo::Echo::type_key() != echo::EchoListener::type_key());
  EXPECT(echo::Echo::chpt_tag() != echo::EchoListener::chpt_tag());

  EchoImpl echo_impl;
  ListenerImpl listener_impl;
  auto echo_h = echo::Echo::NameOnHeap(&echo_impl);
  auto listener_h = echo::EchoListener::NameOnHeap(&listener_impl);
  EXPECT(echo::Echo::FromHandle(echo_h) == &echo_impl);
  EXPECT(echo::EchoListener::FromHandle(listener_h) == &listener_impl);
  EXPECT(echo::Echo::FromHandle(listener_h) == nullptr);
  EXPECT(echo::EchoListener::FromHandle(echo_h) == nullptr);
  const mojo::internal::InternedTypeRec* rec =
      mojo::internal::InternedType(echo::Echo::type_key());
  EXPECT(rec != nullptr);
  EXPECT(rec->type_key == echo::Echo::type_key());
  EXPECT(mojo::internal::TypeCage().Contains(rec));
  mojo::internal::FreeObject(echo_h);
  mojo::internal::FreeObject(listener_h);
}

TEST(golden_bound_remote_is_named_on_chpt) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  EXPECT(remote.chpt_handle() != cppgc::internal::CppHeapPointerTable::kNullHandle);
  EXPECT(mojo::Remote<echo::Echo>::FromHandle(remote.chpt_handle()) ==
         remote.get());
  EXPECT(mojo::Remote<echo::EchoListener>::FromHandle(remote.chpt_handle()) ==
         nullptr);
  EXPECT(mojo::Receiver<echo::Echo>::FromHandle(receiver.chpt_handle()) ==
         &impl);
}

// The method ordinal is the message name. The proto is EchoString's
// signature, which is how the stub reads the payload. The object is the
// impl named on the cpp heap. With no client attached, the router accepts
// the message and does not call anything.
TEST(golden_ordinal_runs_the_bound_object) {
  EchoImpl impl;
  mojo::ScopedMessagePipeHandle end_a;
  mojo::ScopedMessagePipeHandle end_b;
  EXPECT_EQ(mojo::CreateMessagePipe(nullptr, &end_a, &end_b), MOJO_RESULT_OK);
  auto router = mojo::MultiplexRouter::Create(std::move(end_a), true);

  mojo::Message dropped(echo::Echo::kEchoStringName,
                        mojo::Message::kFlagExpectsResponse);
  mojo::internal::WriteString(&dropped, std::string("ab"));
  dropped.set_request_id(1);
  EXPECT(router->Accept(&dropped));
  EXPECT_EQ(impl.call_count, 0);

  echo::Echo::Stub_ stub(&impl, router.get());
  router->AttachPrimaryClient(&stub);
  auto heap = echo::Echo::NameOnHeap(&impl);
  EXPECT(echo::Echo::FromHandle(heap) == &impl);

  mojo::Message call(echo::Echo::kEchoStringName,
                     mojo::Message::kFlagExpectsResponse);
  mojo::internal::WriteString(&call, std::string("ab"));
  call.set_request_id(2);
  EXPECT(stub.Accept(&call));
  EXPECT_EQ(impl.call_count, 1);
  EXPECT_EQ(impl.last_in, "ab");
  mojo::internal::FreeObject(heap);
  router->DetachPrimaryClient();
}

TEST(golden_request_response_roundtrip) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  EXPECT(remote.is_bound());
  EXPECT(receiver.is_bound());

  std::string got;
  bool responded = false;
  remote->EchoString("ab", [&](std::string out) {
    got = out;
    responded = true;
  });

  Pump();

  EXPECT(responded);
  EXPECT_EQ(got, "abab");
  EXPECT_EQ(impl.call_count, 1);
}

TEST(golden_associated_listener_roundtrip) {
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

  echo_remote->SetListener(std::move(listener_remote));
  listener_receiver_obj.Bind(std::move(listener_receiver));
  EXPECT(listener_receiver_obj.is_bound());

  Pump();
  EXPECT(true);  // SetListener delivered without Accept() rejecting it.

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

// Proves MultiplexRouter's router-level pipe-control-message protocol
// (peer-endpoint-closed propagation, see WASMCadidumBindings'
// lib/multiplex_router.h) actually fires against a real generated
// Proxy_/Stub_ pair -- not just the hand-rolled Echo used by WCB's own
// tests/bindings/associated_roundtrip.cc.
TEST(golden_associated_disconnect_handler_fires_across_generated_code) {
  EchoImpl echo_impl;
  mojo::Receiver<echo::Echo> echo_receiver(&echo_impl);
  mojo::Remote<echo::Echo> echo_remote;
  echo_receiver.Bind(echo_remote.BindNewPipeAndPassReceiver());

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);

  mojo::AssociatedGroup group = echo_remote.associated_group();
  mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote;
  mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver =
      listener_remote.InitWithNewEndpointAndPassReceiver(group);

  bool client_saw_peer_closed = false;
  listener_receiver_obj.set_disconnect_handler(
      [&] { client_saw_peer_closed = true; });

  echo_remote->SetListener(std::move(listener_remote));
  listener_receiver_obj.Bind(std::move(listener_receiver));
  Pump();
  EXPECT(echo_impl.listener_bound());
  EXPECT(!client_saw_peer_closed);

  echo_impl.ResetListener();
  Pump();

  EXPECT(client_saw_peer_closed);
}

// The invitation is the process. Accept hands that process a named pipe.
// Bind arms the stub. EchoString's ordinal is the message name. The stub
// reads the payload and calls the object. The object is on CHPT, the
// callee on EPT, the type record on TPT. The call loads all three.
TEST(golden_invitation_runs_the_function) {
  struct Work {
    std::string in;
    std::string out;
  };
  auto write_reply = [](Work* work) { work->out = work->in + work->in; };
  using Fn = void (*)(Work*);

  class ProcessEcho : public echo::Echo {
   public:
    v8::internal::ExternalPointerTable* ept = nullptr;
    v8::CppHeapPointerHandle chpt = 0;
    v8::internal::ExternalPointerHandle eph = 0;
    int call_count = 0;
    std::string last_in;
    std::string last_out;

    void EchoString(const std::string& in,
                    base::OnceCallback<void(std::string)> callback) override {
      const void* type_key = echo::Echo::type_key();
      void* obj = mojo::internal::GetObject(chpt, type_key);
      void* raw = ept->Get(
          eph, v8::internal::ExternalPointerTag::kFirstManagedResourceTag);
      const mojo::internal::InternedTypeRec* rec =
          mojo::internal::InternedType(type_key);
      Fn fn = reinterpret_cast<Fn>(raw);
      if (obj != this || fn == nullptr || rec == nullptr ||
          rec->type_key != type_key) {
        callback(std::string());
        return;
      }
      Work work;
      work.in = in;
      fn(&work);
      ++call_count;
      last_in = work.in;
      last_out = work.out;
      callback(work.out);
    }
    void SetListener(mojo::PendingAssociatedRemote<echo::EchoListener>) override {}
  };

  mojo::LoopbackChannel send_channel(0xE010);
  mojo::OutgoingInvitation outgoing;
  mojo::ScopedMessagePipeHandle caller_pipe = outgoing.AttachMessagePipe("echo");
  EXPECT(caller_pipe.is_valid());
  EXPECT_EQ(mojo::OutgoingInvitation::Send(std::move(outgoing), send_channel),
            MOJO_RESULT_OK);

  mojo::LoopbackChannel accept_channel(0xE010);
  mojo::IncomingInvitation incoming =
      mojo::IncomingInvitation::Accept(accept_channel);
  EXPECT(incoming.is_valid());
  mojo::ScopedMessagePipeHandle process_pipe =
      incoming.ExtractMessagePipe("echo");
  EXPECT(process_pipe.is_valid());

  ProcessEcho impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  receiver.Bind(mojo::PendingReceiver<echo::Echo>(std::move(process_pipe)));
  EXPECT(receiver.is_bound());

  v8::internal::ExternalPointerTable ept;
  impl.ept = &ept;
  impl.chpt = receiver.chpt_handle();
  impl.eph = ept.AllocateAndInitializeEntry(
      reinterpret_cast<void*>(+write_reply),
      v8::internal::ExternalPointerTag::kFirstManagedResourceTag);
  EXPECT(echo::Echo::FromHandle(impl.chpt) == &impl);
  EXPECT(mojo::internal::InternedType(echo::Echo::type_key()) != nullptr);

  mojo::Remote<echo::Echo> remote;
  remote.Bind(mojo::PendingRemote<echo::Echo>(std::move(caller_pipe), 0));
  EXPECT(remote.is_bound());

  std::string got;
  bool responded = false;
  remote->EchoString("ab", [&](std::string out) {
    got = std::move(out);
    responded = true;
  });
  Pump();

  EXPECT(responded);
  EXPECT_EQ(got, "abab");
  EXPECT_EQ(impl.call_count, 1);
  EXPECT_EQ(impl.last_in, "ab");
  EXPECT_EQ(impl.last_out, "abab");
  ept.FreeEntry(impl.eph);
}
