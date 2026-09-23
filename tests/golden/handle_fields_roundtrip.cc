// Proves real-mojom-parity phase 9 (v23): handle-bearing struct fields
// (and a method using Chromium `Name@N(...)` ordinals) actually transit
// a live pending_remote inside a struct, through generated Write/Read.
#include "test.h"

#include "handle_fields_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

namespace {

class PingbackImpl : public handle_fields::Pingback {
 public:
  void Ping() override { ++ping_count; }
  int ping_count = 0;
};

class CourierImpl : public handle_fields::Courier {
 public:
  void Deliver(handle_fields::PipeBundle bundle,
               base::OnceCallback<void(bool)> callback) override {
    ++deliver_calls;
    last_bundle = std::move(bundle);
    callback(true);
  }
  void Attach(handle_fields::Attachment a) override {
    ++attach_calls;
    last_attachment_tag = a.which();
  }
  void FanOut(v8::internal::CageVector<mojo::PendingRemote<handle_fields::Pingback>> pings)
      override {
    ++fanout_calls;
    last_fanout_count = static_cast<int>(pings.size());
    last_fanout = std::move(pings);
  }

  int deliver_calls = 0;
  handle_fields::PipeBundle last_bundle;
  int attach_calls = 0;
  handle_fields::Attachment::Tag last_attachment_tag =
      handle_fields::Attachment::Tag::none;
  int fanout_calls = 0;
  int last_fanout_count = 0;
  v8::internal::CageVector<mojo::PendingRemote<handle_fields::Pingback>> last_fanout;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_handle_bearing_struct_transmits_live_pending_remote) {
  CourierImpl impl;
  mojo::Receiver<handle_fields::Courier> receiver(&impl);
  mojo::Remote<handle_fields::Courier> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  PingbackImpl ping_impl;
  mojo::Receiver<handle_fields::Pingback> ping_receiver(&ping_impl);
  handle_fields::PipeBundle bundle;
  bundle.ping = ping_receiver.BindNewPipeAndPassRemote();

  bool ok = false;
  remote->Deliver(std::move(bundle), [&](bool r) { ok = r; });
  Pump();

  EXPECT_EQ(impl.deliver_calls, 1);
  EXPECT(ok);
  EXPECT(impl.last_bundle.ping.is_valid());

  mojo::Remote<handle_fields::Pingback> ping;
  ping.Bind(std::move(impl.last_bundle.ping));
  ping->Ping();
  Pump();
  EXPECT_EQ(ping_impl.ping_count, 1);
}

TEST(golden_handle_bearing_array_param_roundtrips) {
  CourierImpl impl;
  mojo::Receiver<handle_fields::Courier> receiver(&impl);
  mojo::Remote<handle_fields::Courier> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  PingbackImpl ping_impl;
  mojo::Receiver<handle_fields::Pingback> ping_receiver(&ping_impl);
  v8::internal::CageVector<mojo::PendingRemote<handle_fields::Pingback>> pings;
  pings.push_back(ping_receiver.BindNewPipeAndPassRemote());
  remote->FanOut(std::move(pings));
  Pump();

  EXPECT_EQ(impl.fanout_calls, 1);
  EXPECT_EQ(impl.last_fanout_count, 1);
  EXPECT(impl.last_fanout[0].is_valid());
}

TEST(golden_extensible_union_default_arm_roundtrips) {
  CourierImpl impl;
  mojo::Receiver<handle_fields::Courier> receiver(&impl);
  mojo::Remote<handle_fields::Courier> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  handle_fields::Attachment a;
  a.set_none(7);
  remote->Attach(std::move(a));
  Pump();
  EXPECT_EQ(impl.attach_calls, 1);
  EXPECT(impl.last_attachment_tag == handle_fields::Attachment::Tag::none);
}
