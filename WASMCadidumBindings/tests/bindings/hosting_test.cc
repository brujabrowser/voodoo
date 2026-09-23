#include "test.h"

#include "echo_interface.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "mojo/public/cpp/bindings/report_bad_message.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/bindings/shared_remote.h"
#include "mojo/public/cpp/bindings/unique_receiver_set.h"
#include "whp/base/executor.h"

#include <memory>
#include <string>

namespace {

class EchoImpl : public echo::Echo {
 public:
  void EchoString(const std::string& in,
                  base::OnceCallback<void(std::string)> callback) override {
    if (in == "bad") {
      mojo::ReportBadMessage("echo rejected");
      return;
    }
    callback(in);
  }
  void SetListener(mojo::PendingAssociatedRemote<echo::EchoListener>) override {
  }
};

void Pump(int n = 6) {
  for (int i = 0; i < n; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(report_bad_message_closes_the_pipe) {
  auto impl = std::make_unique<EchoImpl>();
  mojo::Receiver<echo::Echo> receiver(impl.get());
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool disconnected = false;
  remote.set_disconnect_handler([&] { disconnected = true; });
  remote->EchoString("bad", [](std::string) {});
  Pump();
  EXPECT(disconnected);
}

TEST(disconnect_with_reason_reaches_the_peer) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  uint32_t reason = 0;
  std::string description;
  bool saw = false;
  remote.set_disconnect_with_reason_handler(
      [&](uint32_t r, const std::string& d) {
        saw = true;
        reason = r;
        description = d;
      });
  receiver.reset_with_reason(7, "going away");
  Pump();
  EXPECT(saw);
  EXPECT_EQ(reason, 7u);
  EXPECT_EQ(description, std::string("going away"));
}

TEST(self_owned_receiver_dies_on_disconnect) {
  mojo::Remote<echo::Echo> remote;
  std::weak_ptr<mojo::SelfOwnedReceiver<echo::Echo>> weak;
  {
    auto impl = std::make_unique<EchoImpl>();
    weak = mojo::SelfOwnedReceiver<echo::Echo>::Create(
        std::move(impl), remote.BindNewPipeAndPassReceiver());
  }
  EXPECT(!weak.expired());
  std::string got;
  remote->EchoString("ok", [&](std::string s) { got = s; });
  Pump();
  EXPECT_EQ(got, std::string("ok"));
  remote.reset();
  Pump();
  EXPECT(weak.expired());
}

TEST(unbind_and_rebind_keeps_the_pipe_alive) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  mojo::PendingRemote<echo::Echo> pending = remote.Unbind();
  EXPECT(pending.is_valid());
  EXPECT(!remote.is_bound());
  remote.Bind(std::move(pending));
  std::string got;
  remote->EchoString("re", [&](std::string s) { got = s; });
  remote.FlushForTesting();
  EXPECT_EQ(got, std::string("re"));
}

TEST(receiver_pause_blocks_incoming_until_resume) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());
  receiver.PauseIncomingMethodCallProcessing();
  std::string got;
  remote->EchoString("paused", [&](std::string s) { got = s; });
  Pump();
  EXPECT_EQ(got, std::string(""));
  receiver.ResumeIncomingMethodCallProcessing();
  Pump();
  EXPECT_EQ(got, std::string("paused"));
}

TEST(shared_remote_copies_talk_to_the_same_impl) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::PendingRemote<echo::Echo> pending;
  receiver.Bind(pending.InitWithNewPipeAndPassReceiver());
  mojo::SharedRemote<echo::Echo> a(std::move(pending));
  mojo::SharedRemote<echo::Echo> b = a;
  std::string got;
  b->EchoString("share", [&](std::string s) { got = s; });
  a.FlushForTesting();
  EXPECT_EQ(got, std::string("share"));
}

TEST(receiver_set_serves_one_impl_on_two_pipes) {
  EchoImpl impl;
  mojo::ReceiverSet<echo::Echo> set;
  mojo::Remote<echo::Echo> a;
  mojo::Remote<echo::Echo> b;
  set.Add(&impl, a.BindNewPipeAndPassReceiver());
  set.Add(&impl, b.BindNewPipeAndPassReceiver());
  EXPECT_EQ(set.size(), 2u);
  std::string got;
  a->EchoString("a", [&](std::string s) { got = s; });
  Pump();
  EXPECT_EQ(got, std::string("a"));
  b.reset();
  Pump();
  EXPECT_EQ(set.size(), 1u);
}

TEST(remote_set_drops_on_peer_close) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::RemoteSet<echo::Echo> set;
  mojo::PendingRemote<echo::Echo> pending;
  receiver.Bind(pending.InitWithNewPipeAndPassReceiver());
  set.Add(std::move(pending));
  EXPECT_EQ(set.size(), 1u);
  receiver.reset();
  Pump();
  EXPECT(set.empty());
}

TEST(generic_pending_receiver_as_echo) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  mojo::PendingReceiver<echo::Echo> pending = remote.BindNewPipeAndPassReceiver();
  mojo::GenericPendingReceiver generic(std::move(pending));
  EXPECT(generic.is_valid());
  auto typed = generic.As<echo::Echo>();
  EXPECT(typed.has_value());
  receiver.Bind(std::move(*typed));
  std::string got;
  remote->EchoString("g", [&](std::string s) { got = s; });
  Pump();
  EXPECT_EQ(got, std::string("g"));
}

TEST(unique_receiver_set_drops_entry_on_disconnect) {
  mojo::UniqueReceiverSet<echo::Echo> set;
  mojo::Remote<echo::Echo> a;
  mojo::Remote<echo::Echo> b;
  set.Add(std::make_unique<EchoImpl>(), a.BindNewPipeAndPassReceiver());
  set.Add(std::make_unique<EchoImpl>(), b.BindNewPipeAndPassReceiver());
  EXPECT_EQ(set.size(), 2u);
  a.reset();
  Pump();
  EXPECT_EQ(set.size(), 1u);
  b.reset();
  Pump();
  EXPECT(set.empty());
}
