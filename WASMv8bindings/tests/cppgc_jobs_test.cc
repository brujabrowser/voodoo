// Golden test: Platform::PostJob / GetForegroundTaskRunner actually run
// work. The unused-jobs bug was dropping JobTask/Task on the floor.
#include <cassert>
#include <cstdio>
#include <memory>

#include "cppgc/default-platform.h"
#include "cppgc/heap.h"
#include "cppgc/platform.h"

namespace {

class CountingJob final : public cppgc::JobTask {
 public:
  explicit CountingJob(int* runs) : runs_(runs) {}

  void Run(cppgc::JobDelegate* delegate) override {
    if (delegate && delegate->ShouldYield()) return;
    ++*runs_;
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return worker_count < 1 ? 1 : 0;
  }

 private:
  int* runs_;
};

class CountingTask final : public cppgc::Task {
 public:
  explicit CountingTask(int* runs) : runs_(runs) {}
  void Run() override { ++*runs_; }

 private:
  int* runs_;
};

}  // namespace

int main() {
  cppgc::DefaultPlatform platform;

  int blocking_runs = 0;
  auto blocking = platform.PostJob(
      cppgc::TaskPriority::kUserBlocking,
      std::make_unique<CountingJob>(&blocking_runs));
  assert(blocking);
  assert(blocking_runs == 1);  // ran on PostJob
  blocking->Join();
  assert(blocking_runs == 1);  // Join is idempotent

  int best_effort_runs = 0;
  auto best = platform.PostJob(
      cppgc::TaskPriority::kBestEffort,
      std::make_unique<CountingJob>(&best_effort_runs));
  assert(best);
  assert(best_effort_runs == 0);  // deferred until Join
  best->Join();
  assert(best_effort_runs == 1);

  auto blocking_runner =
      platform.GetForegroundTaskRunner(cppgc::TaskPriority::kUserBlocking);
  auto best_runner =
      platform.GetForegroundTaskRunner(cppgc::TaskPriority::kBestEffort);
  assert(blocking_runner);
  assert(best_runner);
  assert(blocking_runner != best_runner);
  assert(!blocking_runner->IdleTasksEnabled());
  assert(best_runner->IdleTasksEnabled());

  int task_runs = 0;
  blocking_runner->PostTask(std::make_unique<CountingTask>(&task_runs));
  assert(task_runs == 1);

  assert(!blocking->IsActive());
  assert(!blocking->IsValid());
  assert(!best->IsActive());
  assert(!best->IsValid());

  int cancel_runs = 0;
  auto cancelled = platform.PostJob(
      cppgc::TaskPriority::kBestEffort,
      std::make_unique<CountingJob>(&cancel_runs));
  assert(cancelled);
  assert(cancelled->IsValid());
  assert(cancelled->IsActive());
  assert(cancel_runs == 0);
  cancelled->Cancel();
  assert(!cancelled->IsActive());
  assert(!cancelled->IsValid());
  cancelled->Join();
  assert(cancel_runs == 0);

  int detach_runs = 0;
  auto detached = platform.PostJob(
      cppgc::TaskPriority::kBestEffort,
      std::make_unique<CountingJob>(&detach_runs));
  detached->CancelAndDetach();
  assert(detach_runs == 0);

  int promoted_runs = 0;
  auto promoted = platform.PostJob(
      cppgc::TaskPriority::kBestEffort,
      std::make_unique<CountingJob>(&promoted_runs));
  assert(promoted_runs == 0);
  assert(promoted->UpdatePriorityEnabled());
  promoted->UpdatePriority(cppgc::TaskPriority::kUserBlocking);
  assert(promoted_runs == 1);
  assert(!promoted->IsActive());

  class DualJob final : public cppgc::JobTask {
   public:
    explicit DualJob(int* runs) : runs_(runs) {}
    void Run(cppgc::JobDelegate* delegate) override {
      assert(delegate);
      assert(!delegate->ShouldYield());
      assert(delegate->GetTaskId() < 2);
      ++*runs_;
    }
    size_t GetMaxConcurrency(size_t worker_count) const override {
      return worker_count < 2 ? 2 : 0;
    }

   private:
    int* runs_;
  };
  int dual_runs = 0;
  auto dual = platform.PostJob(cppgc::TaskPriority::kUserBlocking,
                               std::make_unique<DualJob>(&dual_runs));
  assert(dual_runs == 2);
  dual->Join();
  assert(dual_runs == 2);

  class JoiningJob final : public cppgc::JobTask {
   public:
    explicit JoiningJob(int* ran) : ran_(ran) {}
    void Run(cppgc::JobDelegate* delegate) override {
      assert(delegate);
      assert(delegate->IsJoiningThread());
      ++*ran_;
    }
    size_t GetMaxConcurrency(size_t worker_count) const override {
      return worker_count < 1 ? 1 : 0;
    }

   private:
    int* ran_;
  };
  int joining_runs = 0;
  auto joining = platform.PostJob(
      cppgc::TaskPriority::kBestEffort,
      std::make_unique<JoiningJob>(&joining_runs));
  joining->Join();
  assert(joining_runs == 1);

  // Heap::Create(nullptr) installs DefaultPlatform; GC posts mark+sweep jobs.
  auto heap = cppgc::Heap::Create(nullptr);
  heap->ForceGarbageCollectionSlow("test", "jobs-backed atomic GC");
  heap.reset();

  std::printf("cppgc_jobs_test: OK\n");
  return 0;
}
