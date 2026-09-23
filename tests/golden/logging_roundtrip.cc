// Proves the `import` codegen path -- v4's cross-file merge, and (v15) real
// per-file cross-namespace generation -- by compiling the generated
// logging_interface_gen.h (from examples/logging/logging.voodoom, which
// imports examples/common/types.voodoom for LogEntry/Severity) against
// real WASMCadidumBindings and actually running it. LogEntry/Severity never
// appear in logging.voodoom itself, and (v15) they're referenced here as
// common::LogEntry/common::Severity, not logging::LogEntry/
// logging::Severity: types.voodoom now generates its own types_gen.h in
// its own `common` namespace, #included from logging_interface_gen.h, so
// this only compiles if that whole chain -- separate header, separate
// namespace, cross-file #include -- actually works, not just the merge.
#include "test.h"

#include "logging_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class LoggerImpl : public logging::Logger {
 public:
  void Log(const common::LogEntry& entry,
           base::OnceCallback<void(bool)> callback) override {
    entries_.push_back(entry);
    ++counts_[entry.severity];
    callback(true);
  }
  void GetSeverityCounts(
      base::OnceCallback<void(v8::internal::CageMap<common::Severity, int32_t>)>
          callback) override {
    callback(counts_);
  }

 private:
  std::vector<common::LogEntry> entries_;
  v8::internal::CageMap<common::Severity, int32_t> counts_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_imported_types_roundtrip) {
  LoggerImpl impl;
  mojo::Receiver<logging::Logger> receiver(&impl);
  mojo::Remote<logging::Logger> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  common::LogEntry e1;
  e1.message = "started";
  e1.severity = common::Severity::INFO;

  common::LogEntry e2;
  e2.message = "disk almost full";
  e2.severity = common::Severity::WARNING;

  common::LogEntry e3;
  e3.message = "disk full";
  e3.severity = common::Severity::WARNING;

  bool log_done = false;
  for (const auto& e : {e1, e2, e3}) {
    log_done = false;
    remote->Log(e, [&](bool) { log_done = true; });
    Pump();
    EXPECT(log_done);
  }

  v8::internal::CageMap<common::Severity, int32_t> counts;
  bool counts_done = false;
  remote->GetSeverityCounts(
      [&](v8::internal::CageMap<common::Severity, int32_t> c) {
        counts = c;
        counts_done = true;
      });
  Pump();

  EXPECT(counts_done);
  EXPECT_EQ(counts.size(), 2u);
  EXPECT_EQ(counts.at(common::Severity::INFO), 1);
  EXPECT_EQ(counts.at(common::Severity::WARNING), 2);
}
