// Proves v16's nested enum -- an `enum` declared inside an `interface` or
// `struct` body -- by compiling the generated nested_enum_gen.h (from
// examples/nested_enum/nested_enum.voodoom) against real
// WASMCadidumBindings and actually running it. Worker_Status/Job_Priority
// are each declared *inside* their own interface/struct (Worker.Status,
// Job.Priority) and referenced from the *other* container via the
// qualified form (Job uses Worker.Status, JobQueue uses Job.Priority) --
// this only compiles if the generator's ordering-independent (mangled,
// namespace-scope) nested-enum codegen actually works regardless of which
// of struct/interface happens to be textually emitted first (see
// cpp_generator.cc's MangledNestedEnumName). Worker_Status/Job_Priority
// are namespace-scope types (nested_enum::Worker_Status, not
// nested_enum::Worker::Status -- see that file's own comment for why),
// referenced fully qualified below like any other generated type.
#include "test.h"

#include "nested_enum_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class WorkerImpl : public nested_enum::Worker {
 public:
  void SetStatus(nested_enum::Worker::Status s) override { status = s; }
  void GetStatusCounts(
      base::OnceCallback<void(
          v8::internal::CageMap<nested_enum::Worker::Status, int32_t>)>
          callback) override {
    callback({{status, 1}});
  }
  void GetSnapshot(
      base::OnceCallback<void(nested_enum::Worker::Snapshot)> callback)
      override {
    nested_enum::Worker::Snapshot s;
    s.status = status;
    s.jobs = 1;
    callback(std::move(s));
  }

  nested_enum::Worker::Status status = nested_enum::Worker::Status::IDLE;
};

class JobQueueImpl : public nested_enum::JobQueue {
 public:
  void Enqueue(const nested_enum::Job& job,
               base::OnceCallback<void(bool)> callback) override {
    jobs.push_back(job);
    callback(true);
  }
  void SetPriority(nested_enum::Job::Priority p) override {
    last_priority = p;
  }

  std::vector<nested_enum::Job> jobs;
  nested_enum::Job::Priority last_priority = nested_enum::Job::Priority::LOW;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_nested_enum_bare_reference_within_own_interface) {
  WorkerImpl impl;
  mojo::Receiver<nested_enum::Worker> receiver(&impl);
  mojo::Remote<nested_enum::Worker> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SetStatus(nested_enum::Worker::Status::RUNNING);
  Pump();
  EXPECT(impl.status == nested_enum::Worker::Status::RUNNING);

  v8::internal::CageMap<nested_enum::Worker::Status, int32_t> counts;
  bool done = false;
  remote->GetStatusCounts(
      [&](v8::internal::CageMap<nested_enum::Worker_Status, int32_t> c) {
        counts = c;
        done = true;
      });
  Pump();
  EXPECT(done);
  EXPECT_EQ(counts.size(), 1u);
  EXPECT_EQ(counts.at(nested_enum::Worker::Status::RUNNING), 1);
}

TEST(golden_nested_struct_in_interface_roundtrip) {
  WorkerImpl impl;
  mojo::Receiver<nested_enum::Worker> receiver(&impl);
  mojo::Remote<nested_enum::Worker> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SetStatus(nested_enum::Worker::Status::DONE);
  Pump();

  nested_enum::Worker::Snapshot got;
  bool done = false;
  remote->GetSnapshot([&](nested_enum::Worker_Snapshot s) {
    got = std::move(s);
    done = true;
  });
  Pump();
  EXPECT(done);
  EXPECT(got.status == nested_enum::Worker::Status::DONE);
  EXPECT_EQ(got.jobs, 1);
}

TEST(golden_nested_enum_qualified_cross_container_reference) {
  JobQueueImpl impl;
  mojo::Receiver<nested_enum::JobQueue> receiver(&impl);
  mojo::Remote<nested_enum::JobQueue> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  // Job (a struct) uses Worker.Status (an interface's nested enum) --
  // proves that direction of cross-container reference round-trips.
  nested_enum::Job job;
  job.priority = nested_enum::Job::Priority::HIGH;
  job.status = nested_enum::Worker::Status::DONE;

  bool ok = false;
  bool done = false;
  remote->Enqueue(job, [&](bool result) {
    ok = result;
    done = true;
  });
  Pump();
  EXPECT(done);
  EXPECT(ok);
  EXPECT_EQ(impl.jobs.size(), 1u);
  if (!impl.jobs.empty()) {
    EXPECT(impl.jobs[0].priority == nested_enum::Job::Priority::HIGH);
    EXPECT(impl.jobs[0].status == nested_enum::Worker::Status::DONE);
  }

  // JobQueue (an interface) uses Job.Priority (a struct's nested enum) --
  // the other cross-container direction.
  remote->SetPriority(nested_enum::Job::Priority::NORMAL);
  Pump();
  EXPECT(impl.last_priority == nested_enum::Job::Priority::NORMAL);
}
