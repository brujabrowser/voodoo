// Proves the v12 codegen path -- QueryVersion/RequireVersion, generated
// unconditionally on every interface -- by compiling generated headers
// already built for the extensibility proof (greeter_v1_closed.voodoom,
// kVersion=0; greeter_v2.voodoom, kVersion=1) against real
// WASMCadidumBindings and actually exchanging control messages over a
// real pipe. Reuses those existing schemas rather than adding new ones,
// since QueryVersion/RequireVersion don't need any .voodoom-level
// attribute to exist at all.
#include "test.h"

#include "greeter_v1_closed_interface_gen.h"
#include "greeter_v2_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

namespace {

class GreeterV1Impl : public greeter_v1_closed::Greeter {
 public:
  void Greet(const std::string& name,
             base::OnceCallback<void(std::string)> callback) override {
    callback("Hello, " + name + "!");
  }
};

class GreeterV2Impl : public greeter_v2::Greeter {
 public:
  void Greet(const std::string& name,
             base::OnceCallback<void(std::string)> callback) override {
    callback("Hello, " + name + "!");
  }
  void Farewell(const std::string& name,
                base::OnceCallback<void(std::string)> callback) override {
    callback("Goodbye, " + name + "!");
  }
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_query_version_reports_v1_servers_own_version) {
  GreeterV1Impl impl;
  mojo::Receiver<greeter_v1_closed::Greeter> receiver(&impl);
  mojo::Remote<greeter_v2::Greeter> remote;
  // Different C++ types (greeter_v2::Greeter vs. greeter_v1_closed::
  // Greeter) -- build the pipe by hand, same as extensibility_roundtrip.cc.
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  remote.Bind(mojo::PendingRemote<greeter_v2::Greeter>(std::move(a), 0));
  receiver.Bind(mojo::PendingReceiver<greeter_v1_closed::Greeter>(std::move(b)));

  uint32_t got_version = 0xFFFFFFFFu;
  bool done = false;
  remote.proxy()->QueryVersion([&](uint32_t v) {
    got_version = v;
    done = true;
  });
  Pump();

  EXPECT(done);
  EXPECT_EQ(got_version, greeter_v1_closed::Greeter::kVersion);
  EXPECT_EQ(got_version, 0u);
}

TEST(golden_query_version_reports_v2_servers_own_version) {
  GreeterV2Impl impl;
  mojo::Receiver<greeter_v2::Greeter> receiver(&impl);
  mojo::Remote<greeter_v2::Greeter> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  uint32_t got_version = 0xFFFFFFFFu;
  bool done = false;
  remote.proxy()->QueryVersion([&](uint32_t v) {
    got_version = v;
    done = true;
  });
  Pump();

  EXPECT(done);
  EXPECT_EQ(got_version, 1u);
}

TEST(golden_require_version_satisfied_leaves_connection_working) {
  GreeterV1Impl impl;
  mojo::Receiver<greeter_v1_closed::Greeter> receiver(&impl);
  mojo::Remote<greeter_v2::Greeter> remote;
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  remote.Bind(mojo::PendingRemote<greeter_v2::Greeter>(std::move(a), 0));
  receiver.Bind(mojo::PendingReceiver<greeter_v1_closed::Greeter>(std::move(b)));

  remote.proxy()->RequireVersion(0);  // the server IS at least version 0
  Pump();

  std::string reply;
  bool greet_done = false;
  remote->Greet("Alice", [&](std::string r) {
    reply = r;
    greet_done = true;
  });
  Pump();

  EXPECT(greet_done);
  EXPECT_EQ(reply, "Hello, Alice!");
}

TEST(golden_require_version_unsatisfied_stops_the_server_responding) {
  GreeterV1Impl impl;
  mojo::Receiver<greeter_v1_closed::Greeter> receiver(&impl);
  mojo::Remote<greeter_v2::Greeter> remote;
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  remote.Bind(mojo::PendingRemote<greeter_v2::Greeter>(std::move(a), 0));
  receiver.Bind(mojo::PendingReceiver<greeter_v1_closed::Greeter>(std::move(b)));

  // RaiseError() now closes the local pipe (Bindings + HolePunch
  // PEER_CLOSED), so both sides observe the disconnect.
  bool server_errored = false;
  bool client_errored = false;
  receiver.set_disconnect_handler([&] { server_errored = true; });
  remote.set_disconnect_handler([&] { client_errored = true; });

  remote.proxy()->RequireVersion(1);  // the server is only version 0
  Pump();

  EXPECT(server_errored);
  EXPECT(client_errored);

  bool greet_done = false;
  remote->Greet("Alice", [&](std::string) { greet_done = true; });
  Pump();

  EXPECT(!greet_done);
}
