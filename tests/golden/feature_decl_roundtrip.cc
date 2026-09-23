// Proves real-mojom-parity phase 7's codegen path -- `feature NAME {
// ... };` top-level declarations -- by compiling the generated
// feature_decl_gen.h (from examples/feature_decl/feature_decl.voodoom)
// against real WASMCadidumBindings and actually using the feature's
// mangled consts at runtime: RetryPolicy_kMaxRetries/kEnabled are real,
// usable C++ constexpr values (not just a parse-time artifact), a
// default-constructed Job picks up RetryPolicy_kMaxRetries as its
// field default and that default survives a real wire round-trip, and
// the same constant is usable directly as an ordinary method argument.
#include "test.h"

#include "feature_decl_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class JobQueueImpl : public feature_decl::JobQueue {
 public:
  void Submit(const feature_decl::Job& job) override {
    ++submit_calls;
    last_job = job;
  }
  void SubmitWithRetries(const std::string& name, int32_t retries,
                          base::OnceCallback<void(bool)> callback) override {
    last_retries = retries;
    callback(retries <= feature_decl::RetryPolicy_kMaxRetries);
  }

  int submit_calls = 0;
  feature_decl::Job last_job;
  int32_t last_retries = -1;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_feature_const_values_are_correct) {
  EXPECT_EQ(feature_decl::RetryPolicy_kMaxRetries, 3);
  EXPECT(feature_decl::RetryPolicy_kEnabled);
}

TEST(golden_struct_field_default_from_feature_const_roundtrips) {
  JobQueueImpl impl;
  mojo::Receiver<feature_decl::JobQueue> receiver(&impl);
  mojo::Remote<feature_decl::JobQueue> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  feature_decl::Job job;
  job.name = "build";
  // job.retries left at its default -- should be RetryPolicy_kMaxRetries.
  EXPECT_EQ(job.retries, 3);

  remote->Submit(job);
  Pump();

  EXPECT_EQ(impl.submit_calls, 1);
  EXPECT_EQ(impl.last_job.name, "build");
  EXPECT_EQ(impl.last_job.retries, feature_decl::RetryPolicy_kMaxRetries);
}

TEST(golden_feature_const_usable_as_method_argument) {
  JobQueueImpl impl;
  mojo::Receiver<feature_decl::JobQueue> receiver(&impl);
  mojo::Remote<feature_decl::JobQueue> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool accepted = false;
  bool done = false;
  remote->SubmitWithRetries("deploy", feature_decl::RetryPolicy_kMaxRetries,
                             [&](bool ok) {
                               accepted = ok;
                               done = true;
                             });
  Pump();

  EXPECT(done);
  EXPECT(accepted);
  EXPECT_EQ(impl.last_retries, 3);
}
