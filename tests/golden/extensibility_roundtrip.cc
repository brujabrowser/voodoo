// Proves the v10 codegen path -- interface `[Extensible]` -- by
// compiling three generated headers (from greeter_v1_extensible.voodoom,
// greeter_v1_closed.voodoom, and greeter_v2.voodoom) against real
// WASMCadidumBindings and actually connecting a "new client" Remote to
// each of the two "old server" Receivers in turn.
//
// The three headers declare genuinely different C++ types
// (greeter_v1_extensible::Greeter, greeter_v1_closed::Greeter,
// greeter_v2::Greeter aren't the same class), so a Remote<greeter_v2::
// Greeter> can't be bound to a Receiver<greeter_v1_extensible::Greeter>
// through the ordinary type-checked Bind(PendingReceiver<Interface>)
// path -- exactly like two real, separately-compiled binaries built from
// two different revisions of one .voodoom file would be, which is the
// point: this constructs the pipe by hand (mojo::CreateMessagePipe +
// PendingRemote<T>(pipe, version)/PendingReceiver<T>(pipe) built directly
// from the raw handles) to actually simulate that.
#include "test.h"

#include "greeter_v1_extensible_interface_gen.h"
#include "greeter_v1_closed_interface_gen.h"
#include "greeter_v2_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

namespace {

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

class ExtensibleGreeterImpl : public greeter_v1_extensible::Greeter {
 public:
  void Greet(const std::string& name,
             base::OnceCallback<void(std::string)> callback) override {
    callback("Hello, " + name + "!");
  }
};

class ClosedGreeterImpl : public greeter_v1_closed::Greeter {
 public:
  void Greet(const std::string& name,
             base::OnceCallback<void(std::string)> callback) override {
    callback("Hello, " + name + "!");
  }
};

}  // namespace

TEST(golden_extensible_server_survives_unknown_method_call) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::Remote<greeter_v2::Greeter> remote;
  remote.Bind(mojo::PendingRemote<greeter_v2::Greeter>(std::move(a), 0));

  ExtensibleGreeterImpl impl;
  mojo::Receiver<greeter_v1_extensible::Greeter> receiver(&impl);
  receiver.Bind(
      mojo::PendingReceiver<greeter_v1_extensible::Greeter>(std::move(b)));

  bool disconnected = false;
  remote.set_disconnect_handler([&] { disconnected = true; });

  // Farewell() -- ordinal 1 -- doesn't exist on the v1 server at all.
  bool farewell_done = false;
  remote->Farewell("Bob", [&](std::string) { farewell_done = true; });
  Pump();

  EXPECT(!disconnected);   // [Extensible]: tolerated, not a protocol error
  EXPECT(!farewell_done);  // ...but obviously never gets an actual reply

  // The connection must still work normally for what the server *does*
  // know about, afterward.
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

TEST(golden_non_extensible_server_stops_responding_on_unknown_method_call) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::Remote<greeter_v2::Greeter> remote;
  remote.Bind(mojo::PendingRemote<greeter_v2::Greeter>(std::move(a), 0));

  ClosedGreeterImpl impl;
  mojo::Receiver<greeter_v1_closed::Greeter> receiver(&impl);
  receiver.Bind(
      mojo::PendingReceiver<greeter_v1_closed::Greeter>(std::move(b)));

  // WASMCadidumBindings' Connector::RaiseError() (connector.cc) doesn't
  // close the underlying pipe -- it only stops *that side's own*
  // processing and fires *that side's own* connection_error_handler_ (see
  // connector.cc; also documented as a simplification vs. real Mojo's own
  // pipe-control-message-propagated errors in WCB's README). So the
  // server-side error is observed on the server's own Receiver, not
  // (falsely) inferred from the client ever getting a disconnect
  // notification it has no real way to receive here.
  bool server_errored = false;
  receiver.set_disconnect_handler([&] { server_errored = true; });

  bool farewell_done = false;
  remote->Farewell("Bob", [&](std::string) { farewell_done = true; });
  Pump();

  EXPECT(server_errored);  // no [Extensible]: an unknown ordinal is fatal
                            // -- but only observable on the side that hit it
  EXPECT(!farewell_done);

  // The server's connector is now in its own error state and has stopped
  // reading anything further off the pipe entirely (see connector.cc's
  // OnReadable: `if (paused_ || error_) return;`) -- so even a call to a
  // method the server *did* know about never gets a response, from here
  // on. This is the actually-observable, client-side consequence.
  bool greet_done = false;
  remote->Greet("Alice", [&](std::string) { greet_done = true; });
  Pump();

  EXPECT(!greet_done);
}

TEST(golden_interface_kversion_constant) {
  EXPECT_EQ(greeter_v1_extensible::Greeter::kVersion, 0u);
  EXPECT_EQ(greeter_v1_closed::Greeter::kVersion, 0u);
  EXPECT_EQ(greeter_v2::Greeter::kVersion, 1u);
}
