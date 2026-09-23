// Proves the codegen for loki-closure's whp_cadmium bridge contract --
// an interface handed a pending_associated_remote *and* returning a
// response in the same call (Attach), plus a push over that associated
// channel (OnFire) and a plain struct-by-value request (Report) -- by
// compiling the generated cadmium_voodoo_interface_gen.h (from
// examples/cadmium_voodoo/cadmium_voodoo.voodoom) against real
// WASMCadidumBindings and actually running it, the same way
// echo_roundtrip.cc proves EchoListener's associated-listener idiom.
#include "test.h"

#include "cadmium_voodoo_interface_gen.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class ListenerImpl : public cadmium::voodoo::VoodooOrdinalListener {
 public:
  void OnFire(uint32_t ordinal, const std::string& name) override {
    seen.push_back({ordinal, name});
  }
  std::vector<std::pair<uint32_t, std::string>> seen;
};

class BridgeImpl : public cadmium::voodoo::VoodooBridge {
 public:
  void Attach(mojo::PendingAssociatedRemote<cadmium::voodoo::VoodooOrdinalListener> listener,
              base::OnceCallback<void(std::string)> callback) override {
    listener_.Bind(std::move(listener));
    callback(sid);
  }
  void Report(const cadmium::voodoo::BridgeEvent& event,
              base::OnceCallback<void(bool)> callback) override {
    last_event = event;
    got_report = true;
    callback(true);
  }

  // What Cadmium does the moment it queues an outbound ordinal, once
  // Attach() has bound a listener -- no polling involved.
  void PushOrdinal(uint32_t ordinal, const std::string& name) {
    if (listener_.is_bound()) {
      listener_->OnFire(ordinal, name);
    }
  }

  bool listener_bound() const { return listener_.is_bound(); }

  std::string sid = "sid-1";
  bool got_report = false;
  cadmium::voodoo::BridgeEvent last_event;

 private:
  mojo::AssociatedRemote<cadmium::voodoo::VoodooOrdinalListener> listener_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_attach_returns_sid_and_pushes_ordinal_over_associated_channel) {
  BridgeImpl bridge_impl;
  mojo::Receiver<cadmium::voodoo::VoodooBridge> bridge_receiver(&bridge_impl);
  mojo::Remote<cadmium::voodoo::VoodooBridge> bridge_remote;
  bridge_receiver.Bind(bridge_remote.BindNewPipeAndPassReceiver());

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<cadmium::voodoo::VoodooOrdinalListener> listener_receiver_obj(
      &listener_impl);

  mojo::AssociatedGroup group = bridge_remote.associated_group();
  EXPECT(group.is_valid());

  mojo::PendingAssociatedRemote<cadmium::voodoo::VoodooOrdinalListener> listener_remote;
  mojo::PendingAssociatedReceiver<cadmium::voodoo::VoodooOrdinalListener> listener_receiver =
      listener_remote.InitWithNewEndpointAndPassReceiver(group);

  std::string got_sid;
  bool attached = false;
  bridge_remote->Attach(std::move(listener_remote), [&](std::string sid) {
    got_sid = sid;
    attached = true;
  });
  listener_receiver_obj.Bind(std::move(listener_receiver));
  Pump();

  EXPECT(attached);
  EXPECT_EQ(got_sid, "sid-1");
  EXPECT(bridge_impl.listener_bound());

  // Cadmium pushes an outbound ordinal the moment it has one -- no
  // /api/rbi/ipc GET poll needed against this channel.
  bridge_impl.PushOrdinal(1234, "Foo");
  Pump();

  EXPECT_EQ(listener_impl.seen.size(), 1u);
  if (!listener_impl.seen.empty()) {
    EXPECT_EQ(listener_impl.seen[0].first, 1234u);
    EXPECT_EQ(listener_impl.seen[0].second, "Foo");
  }
}

TEST(golden_report_struct_param_roundtrip) {
  BridgeImpl bridge_impl;
  mojo::Receiver<cadmium::voodoo::VoodooBridge> bridge_receiver(&bridge_impl);
  mojo::Remote<cadmium::voodoo::VoodooBridge> bridge_remote;
  bridge_receiver.Bind(bridge_remote.BindNewPipeAndPassReceiver());

  cadmium::voodoo::BridgeEvent ev;
  ev.kind = "mojo_pipe";
  ev.via = "holepunch";
  ev.ordinal = 5678;
  ev.name = "Bar";
  ev.sid = "sid-2";
  ev.note = "fired";

  bool ok = false;
  bool done = false;
  bridge_remote->Report(ev, [&](bool result) {
    ok = result;
    done = true;
  });
  Pump();

  EXPECT(done);
  EXPECT(ok);
  EXPECT(bridge_impl.got_report);
  EXPECT_EQ(bridge_impl.last_event.kind, "mojo_pipe");
  EXPECT_EQ(bridge_impl.last_event.via, "holepunch");
  EXPECT_EQ(bridge_impl.last_event.ordinal, 5678u);
  EXPECT_EQ(bridge_impl.last_event.name, "Bar");
  EXPECT_EQ(bridge_impl.last_event.sid, "sid-2");
  EXPECT_EQ(bridge_impl.last_event.note, "fired");
}
