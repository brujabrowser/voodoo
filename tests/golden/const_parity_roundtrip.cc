// Proves real-mojom-parity phase 2 -- nested `const` and the widened
// const/default value grammar (float/double/bool/string, hex literals) --
// by compiling the generated const_parity_gen.h (from
// examples/const_parity/const_parity.voodoom) against real
// WASMCadidumBindings and actually running it. Config's field defaults
// (timeout/retries/mask/threshold/pi/debug/greeting) come from a mix of
// top-level and nested consts -- if any of the value parsing/codegen were
// wrong (wrong literal, wrong qualifier, wrong emission order for
// Config_kDefaultTimeout's reference to Worker_kMaxJobs), this wouldn't
// even compile or the roundtripped values wouldn't match what the
// .voodoom source declares.
#include "test.h"

#include "const_parity_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class WorkerImpl : public const_parity::Worker {
 public:
  void GetMaxJobs(base::OnceCallback<void(int32_t)> callback) override {
    callback(const_parity::Worker_kMaxJobs);
  }
};

class ConfigStoreImpl : public const_parity::ConfigStore {
 public:
  void GetConfig(base::OnceCallback<void(const_parity::Config)> callback) override {
    callback(const_parity::Config{});  // every field left at its default
  }
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_nested_const_qualified_cross_container_reference) {
  WorkerImpl impl;
  mojo::Receiver<const_parity::Worker> receiver(&impl);
  mojo::Remote<const_parity::Worker> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  int32_t jobs = 0;
  bool done = false;
  remote->GetMaxJobs([&](int32_t j) {
    jobs = j;
    done = true;
  });
  Pump();
  EXPECT(done);
  EXPECT_EQ(jobs, 10);
}

TEST(golden_const_typed_field_defaults_roundtrip) {
  ConfigStoreImpl impl;
  mojo::Receiver<const_parity::ConfigStore> receiver(&impl);
  mojo::Remote<const_parity::ConfigStore> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  const_parity::Config got;
  bool done = false;
  remote->GetConfig([&](const_parity::Config c) {
    got = c;
    done = true;
  });
  Pump();

  EXPECT(done);
  // Config.timeout defaults from Config's own nested const
  // kDefaultTimeout, whose value is Worker.kMaxJobs (a *different*
  // container's nested const) -- proves the cross-container const
  // reference round-trips, not just compiles.
  EXPECT_EQ(got.timeout, 10);
  EXPECT_EQ(got.retries, 5);
  EXPECT_EQ(got.mask, 255);  // from `const int32 kMask = 0xFF;`
  EXPECT_EQ(got.threshold, 3.5f);
  EXPECT_EQ(got.pi, 3.14159265358979);
  EXPECT(got.debug);
  EXPECT_EQ(got.greeting, "hello");
}
