#include "whp/base/executor.h"

#include <chrono>

namespace whp {

Executor& Executor::Current() {
  static Executor exec;
  return exec;
}

void Executor::PostTask(OnceClosure task) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void Executor::SetIdleSource(IdleSource source) { idle_source_ = source; }

void Executor::RunUntilIdle() {
  for (;;) {
    if (idle_source_) {
      idle_source_();
    }
    OnceClosure task;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (queue_.empty()) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    std::move(task).Run();
  }
}

void Executor::Run() {
  quit_ = false;
  while (!quit_) {
    if (idle_source_) {
      idle_source_();
    }
    OnceClosure task;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (queue_.empty()) {
        return;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    std::move(task).Run();
  }
}

void Executor::Quit() { quit_ = true; }

bool Executor::WaitForWork(TimeDelta timeout) {
  std::unique_lock<std::mutex> lock(mu_);
  if (!queue_.empty()) {
    return true;
  }
  if (timeout.is_max()) {
    cv_.wait(lock, [this] { return !queue_.empty(); });
    return true;
  }
  return cv_.wait_for(lock,
                      std::chrono::microseconds(timeout.InMicroseconds()),
                      [this] { return !queue_.empty(); });
}

}  // namespace whp
