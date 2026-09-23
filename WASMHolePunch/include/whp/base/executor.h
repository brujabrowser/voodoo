#ifndef WHP_BASE_EXECUTOR_H_
#define WHP_BASE_EXECUTOR_H_

#include "whp/base/once_callback.h"
#include "whp/base/time.h"

#include <condition_variable>
#include <deque>
#include <mutex>

namespace whp {

// Single-thread FIFO task queue. WASI has no thread pool; native tests use
// the same model so dispatch order matches in-process Mojo. WaitForWork
// is the blocking wait used by WASMv8Bindings JobHandle::Join (SyncWaitFor).
class Executor {
 public:
  static Executor& Current();

  void PostTask(OnceClosure task);
  void RunUntilIdle();
  void Run();
  void Quit();
  // Blocks until a task is queued or `timeout` elapses. Max() waits forever.
  bool WaitForWork(TimeDelta timeout);

  // Extension point for an async bridge's own pump (e.g. whp/system's
  // WhpPumpEvents) -- called at the top of every RunUntilIdle()/Run()
  // iteration, before checking queue_, so "call RunUntilIdle() until
  // things settle" keeps working as the one call site every consumer
  // already uses, without whp/base itself depending on whp/system (which
  // depends on whp/base already -- registering a hook here instead of
  // #including whp/c/system.h keeps that one-directional). Last
  // registration wins; at most one source is expected in practice (one
  // process-wide whp::system::Core).
  using IdleSource = void (*)();
  void SetIdleSource(IdleSource source);

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<OnceClosure> queue_;
  bool quit_ = false;
  IdleSource idle_source_ = nullptr;
};

}  // namespace whp

#endif  // WHP_BASE_EXECUTOR_H_
