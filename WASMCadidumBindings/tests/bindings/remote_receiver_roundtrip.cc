#include "test.h"

#include "echo_interface.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class EchoImpl : public echo::Echo {
 public:
  void EchoString(const std::string& in,
                   base::OnceCallback<void(std::string)> callback) override {
    ++call_count;
    callback(in + in);  // doubled, so the test can tell request from response
  }
  void SetListener(mojo::PendingAssociatedRemote<echo::EchoListener>) override {
    // Not exercised by this test; see associated_roundtrip.cc.
  }

  int call_count = 0;
};

void Pump(int iterations = 4) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(remote_receiver_request_response) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  EXPECT(remote.is_bound());
  EXPECT(receiver.is_bound());
  EXPECT(mojo::Remote<echo::Echo>::FromHandle(remote.chpt_handle()) ==
         remote.get());
  EXPECT(mojo::Receiver<echo::Echo>::FromHandle(receiver.chpt_handle()) ==
         &impl);

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

TEST(remote_receiver_multiple_calls_get_matched_responses) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  std::vector<std::string> responses;
  remote->EchoString("x", [&](std::string out) { responses.push_back(out); });
  remote->EchoString("y", [&](std::string out) { responses.push_back(out); });
  remote->EchoString("z", [&](std::string out) { responses.push_back(out); });

  Pump();

  EXPECT_EQ(responses.size(), 3u);
  // Requests are independent, but each response must pair with its own
  // request via request_id, not arrive in some scrambled order-dependent way.
  bool has_xx = false, has_yy = false, has_zz = false;
  for (auto& r : responses) {
    if (r == "xx") has_xx = true;
    if (r == "yy") has_yy = true;
    if (r == "zz") has_zz = true;
  }
  EXPECT(has_xx);
  EXPECT(has_yy);
  EXPECT(has_zz);
}

TEST(remote_disconnect_handler_fires_on_receiver_reset) {
  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool disconnected = false;
  remote.set_disconnect_handler([&disconnected] { disconnected = true; });

  receiver.reset();
  Pump();

  EXPECT(disconnected);
}
