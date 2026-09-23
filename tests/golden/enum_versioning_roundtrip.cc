// Proves the v13 codegen path -- enum `[Extensible]` -- by compiling
// three generated headers (from color_v1_closed.voodoom,
// color_v1_extensible.voodoom, and color_v2.voodoom) against real
// WASMCadidumBindings and actually connecting a "new client" Remote
// (whose Color has kRed/kGreen/kBlue) to each of the two "old server"
// Receivers (whose Color only has kRed/kGreen) in turn, exactly the same
// hand-built-pipe technique extensibility_roundtrip.cc uses for interface
// `[Extensible]` -- see that file's header comment for why a raw pipe is
// needed here (the three headers declare genuinely different C++ types).
#include "test.h"

#include "color_v1_closed_interface_gen.h"
#include "color_v1_extensible_interface_gen.h"
#include "color_v2_interface_gen.h"
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

class ClosedPainterImpl : public color_v1_closed::Painter {
 public:
  void SetColor(color_v1_closed::Color c) override {
    ++set_color_calls;
    last_color = c;
  }
  void Ping(base::OnceCallback<void(bool)> callback) override { callback(true); }

  int set_color_calls = 0;
  color_v1_closed::Color last_color = color_v1_closed::Color::kRed;
};

class ExtensiblePainterImpl : public color_v1_extensible::Painter {
 public:
  void SetColor(color_v1_extensible::Color c) override {
    ++set_color_calls;
    last_color = c;
  }
  void Ping(base::OnceCallback<void(bool)> callback) override { callback(true); }

  int set_color_calls = 0;
  color_v1_extensible::Color last_color = color_v1_extensible::Color::kRed;
};

}  // namespace

TEST(golden_non_extensible_enum_server_stops_responding_on_unknown_value) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::Remote<color_v2::Painter> remote;
  remote.Bind(mojo::PendingRemote<color_v2::Painter>(std::move(a), 0));

  ClosedPainterImpl impl;
  mojo::Receiver<color_v1_closed::Painter> receiver(&impl);
  receiver.Bind(
      mojo::PendingReceiver<color_v1_closed::Painter>(std::move(b)));

  // Same "observe the error on the side that actually hit it" pattern as
  // extensibility_roundtrip.cc -- Connector::RaiseError() doesn't
  // propagate across the pipe (see connector.cc and README's "Interface
  // versioning").
  bool server_errored = false;
  receiver.set_disconnect_handler([&] { server_errored = true; });

  // kBlue -- a value neither v1 Color has ever heard of.
  remote->SetColor(color_v2::Color::kBlue);
  Pump();

  EXPECT(server_errored);       // non-extensible: an unknown enum value on
                                  // the wire fails the read, same as an
                                  // unknown method ordinal would
  EXPECT_EQ(impl.set_color_calls, 0);

  // The server's connector has stopped reading anything further off the
  // pipe (see connector.cc), so even Ping() -- a method it does know --
  // never gets a response from here on.
  bool ping_done = false;
  remote->Ping([&](bool) { ping_done = true; });
  Pump();

  EXPECT(!ping_done);
}

TEST(golden_extensible_enum_server_tolerates_unknown_value) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::Remote<color_v2::Painter> remote;
  remote.Bind(mojo::PendingRemote<color_v2::Painter>(std::move(a), 0));

  ExtensiblePainterImpl impl;
  mojo::Receiver<color_v1_extensible::Painter> receiver(&impl);
  receiver.Bind(
      mojo::PendingReceiver<color_v1_extensible::Painter>(std::move(b)));

  bool disconnected = false;
  remote.set_disconnect_handler([&] { disconnected = true; });

  remote->SetColor(color_v2::Color::kBlue);
  Pump();

  EXPECT(!disconnected);            // [Extensible]: tolerated, not an error
  EXPECT_EQ(impl.set_color_calls, 1);  // the call still reached the impl --
                                         // its Color just holds the raw,
                                         // unnamed wire value (2)
  EXPECT_EQ(static_cast<int32_t>(impl.last_color), 2);

  // The connection must still work normally afterward.
  bool ping_done = false;
  bool ok = false;
  remote->Ping([&](bool v) {
    ping_done = true;
    ok = v;
  });
  Pump();

  EXPECT(ping_done);
  EXPECT(ok);
}

TEST(golden_enum_is_known_helper_rejects_and_accepts_correctly) {
  EXPECT(color_v1_closed::IsKnownColor(0));
  EXPECT(color_v1_closed::IsKnownColor(1));
  EXPECT(!color_v1_closed::IsKnownColor(2));

  EXPECT(color_v2::IsKnownColor(0));
  EXPECT(color_v2::IsKnownColor(1));
  EXPECT(color_v2::IsKnownColor(2));
  EXPECT(!color_v2::IsKnownColor(3));
}
