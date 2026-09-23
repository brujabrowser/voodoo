// Proves the v3 codegen paths -- union (both arms) and map<K, V>, including
// a union and a map as sibling struct fields -- by compiling the generated
// settings_interface_gen.h (from examples/settings/settings.voodoom)
// against real WASMCadidumBindings and actually running it.
#include "test.h"

#include "settings_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class SettingsImpl : public settings::Settings {
 public:
  void Put(const settings::Config& config,
           base::OnceCallback<void(bool)> callback) override {
    stored_ = config;
    has_stored_ = true;
    callback(true);
  }
  void Get(base::OnceCallback<void(settings::Config)> callback) override {
    callback(has_stored_ ? stored_ : settings::Config{});
  }

 private:
  settings::Config stored_;
  bool has_stored_ = false;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_union_int_arm_and_map_roundtrip) {
  SettingsImpl impl;
  mojo::Receiver<settings::Settings> receiver(&impl);
  mojo::Remote<settings::Settings> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  settings::Config c;
  c.name = "prod";
  c.counters["requests"] = 10;
  c.counters["errors"] = 2;
  c.result.set_code(200);

  bool put_ok = false;
  bool put_done = false;
  remote->Put(c, [&](bool ok) {
    put_ok = ok;
    put_done = true;
  });
  Pump();
  EXPECT(put_done);
  EXPECT(put_ok);

  settings::Config got;
  bool get_done = false;
  remote->Get([&](settings::Config config) {
    got = config;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT_EQ(got.name, "prod");
  EXPECT_EQ(got.counters.size(), 2u);
  EXPECT_EQ(got.counters.at("requests"), 10);
  EXPECT_EQ(got.counters.at("errors"), 2);
  EXPECT(got.result.which() == settings::Outcome::Tag::code);
  EXPECT_EQ(got.result.code(), 200);
}

TEST(golden_union_string_arm_and_empty_map_roundtrip) {
  SettingsImpl impl;
  mojo::Receiver<settings::Settings> receiver(&impl);
  mojo::Remote<settings::Settings> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  settings::Config c;
  c.name = "empty";
  c.result.set_message("no data");

  bool put_done = false;
  remote->Put(c, [&](bool) { put_done = true; });
  Pump();
  EXPECT(put_done);

  settings::Config got;
  bool get_done = false;
  remote->Get([&](settings::Config config) {
    got = config;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT(got.counters.empty());
  EXPECT(got.result.which() == settings::Outcome::Tag::message);
  EXPECT_EQ(got.result.message(), "no data");
}
