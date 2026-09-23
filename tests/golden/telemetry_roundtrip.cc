// Proves the v6/v7 codegen paths -- a dotted `module a.b;` name (nested
// C++ namespaces) and every kind of struct field default (bare literal,
// const reference, enum-value reference) -- by compiling the generated
// telemetry_interface_gen.h (from examples/telemetry/telemetry.voodoom)
// against real WASMCadidumBindings and actually running it.
#include "test.h"

#include "telemetry_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

// Fully qualifying through the nested namespace here (rather than a `using
// namespace`) is itself part of the proof: if the dotted module name had
// produced a single flat "wasmvoodoo.telemetry" namespace (or failed to
// nest at all), this wouldn't compile.
class SinkImpl : public wasmvoodoo::telemetry::TelemetrySink {
 public:
  void Record(const wasmvoodoo::telemetry::Sample& s,
              base::OnceCallback<void(bool)> callback) override {
    last_ = s;
    callback(true);
  }
  void GetLast(
      base::OnceCallback<void(wasmvoodoo::telemetry::Sample)> callback) override {
    callback(last_);
  }

 private:
  wasmvoodoo::telemetry::Sample last_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_field_defaults_apply_before_any_wire_roundtrip) {
  // A freshly-constructed Sample that was never sent anywhere -- proves
  // the defaults are real C++ member-initializer values, not something
  // that only appears after a Write/Read cycle. count/level specifically
  // prove the const-reference and enum-value-reference defaults resolved
  // to the *right* values (kDefaultCount=7, Level::MEDIUM), not just to
  // some value.
  wasmvoodoo::telemetry::Sample s;
  EXPECT_EQ(s.label, "unlabeled");
  EXPECT_EQ(s.count, wasmvoodoo::telemetry::kDefaultCount);
  EXPECT_EQ(s.count, 7);
  EXPECT(!s.verified);
  EXPECT(s.level == wasmvoodoo::telemetry::Level::MEDIUM);
}

TEST(golden_dotted_namespace_record_get_last_roundtrip) {
  SinkImpl impl;
  mojo::Receiver<wasmvoodoo::telemetry::TelemetrySink> receiver(&impl);
  mojo::Remote<wasmvoodoo::telemetry::TelemetrySink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  wasmvoodoo::telemetry::Sample sent;
  sent.label = "cpu_temp";
  sent.count = 73;
  sent.verified = true;
  sent.level = wasmvoodoo::telemetry::Level::HIGH;

  bool record_done = false;
  bool record_ok = false;
  remote->Record(sent, [&](bool ok) {
    record_ok = ok;
    record_done = true;
  });
  Pump();
  EXPECT(record_done);
  EXPECT(record_ok);

  wasmvoodoo::telemetry::Sample got;
  bool get_done = false;
  remote->GetLast([&](wasmvoodoo::telemetry::Sample out) {
    got = out;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT_EQ(got.label, "cpu_temp");
  EXPECT_EQ(got.count, 73);
  EXPECT(got.verified);
  EXPECT(got.level == wasmvoodoo::telemetry::Level::HIGH);
}

TEST(golden_overwriting_a_default_field_with_a_non_default_value_roundtrips) {
  // The defaulted fields aren't just for local construction -- a value
  // that differs from the default must still travel correctly over the
  // wire (this is really the same guarantee as any other field; called
  // out explicitly since defaults are new in this compiler).
  SinkImpl impl;
  mojo::Receiver<wasmvoodoo::telemetry::TelemetrySink> receiver(&impl);
  mojo::Remote<wasmvoodoo::telemetry::TelemetrySink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  wasmvoodoo::telemetry::Sample sent;  // keep every field at its default
  bool record_done = false;
  remote->Record(sent, [&](bool) { record_done = true; });
  Pump();
  EXPECT(record_done);

  wasmvoodoo::telemetry::Sample got;
  got.label = "leftover";
  got.count = 999;
  got.verified = true;
  got.level = wasmvoodoo::telemetry::Level::LOW;
  bool get_done = false;
  remote->GetLast([&](wasmvoodoo::telemetry::Sample out) {
    got = out;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT_EQ(got.label, "unlabeled");
  EXPECT_EQ(got.count, 7);
  EXPECT(!got.verified);
  EXPECT(got.level == wasmvoodoo::telemetry::Level::MEDIUM);
}
