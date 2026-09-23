// Minimal stand-in for V8's include/v8-platform.h -- only the pieces
// cppgc/platform.h re-exports (`using X = v8::X;`) and that this port's
// GCInfoTable/Heap actually call. Real V8's PageAllocator manages OS virtual
// memory (mmap-style reservations with page-granularity protection); under
// wasm32-wasip1 there is no such thing -- linear memory is one flat,
// already-readable-and-writable region with no page protection at all. So
// DefaultPageAllocator (src/heap/cppgc-internal/platform.cc) backs
// AllocatePages/ReleasePages with plain new[]/delete[] and makes
// SetPermissions an always-succeeds no-op: there is nothing to protect
// against on this target that WASM's own sandboxing doesn't already cover.
#ifndef WASMV8_INCLUDE_V8_PLATFORM_H_
#define WASMV8_INCLUDE_V8_PLATFORM_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "v8config.h"

namespace v8 {

class PageAllocator {
 public:
  enum Permission {
    kNoAccess,
    kRead,
    kReadWrite,
    kReadWriteExecute,
    kReadExecute,
    kNoAccessWillJitLater
  };

  virtual ~PageAllocator() = default;

  virtual size_t AllocatePageSize() = 0;
  virtual size_t CommitPageSize() { return AllocatePageSize(); }
  virtual void SetRandomMmapSeed(int64_t) {}
  virtual void* GetRandomMmapAddr() { return nullptr; }

  virtual void* AllocatePages(void* address, size_t length, size_t alignment,
                              Permission permissions) = 0;
  virtual bool FreePages(void* address, size_t length) = 0;
  virtual bool ReleasePages(void* address, size_t length,
                            size_t new_length) = 0;
  virtual bool SetPermissions(void* address, size_t length,
                              Permission permissions) = 0;
  virtual bool DecommitPages(void* address, size_t size) = 0;
};

enum class TaskPriority : uint8_t { kBestEffort, kUserVisible, kUserBlocking };

class Task {
 public:
  virtual ~Task() = default;
  virtual void Run() = 0;
};

class IdleTask {
 public:
  virtual ~IdleTask() = default;
  virtual void Run(double deadline_in_seconds) = 0;
};

class TaskRunner {
 public:
  virtual ~TaskRunner() = default;
  // Default: run on the posting thread. wasm32-wasip1 has no worker pool
  // to defer to; dropping the task would be the unused-jobs bug.
  virtual void PostTask(std::unique_ptr<Task> task) {
    if (task) task->Run();
  }
  virtual bool IdleTasksEnabled() { return false; }
};

// Job APIs match real V8 include/v8-platform.h. This port has no thread
// pool (wasm32-wasip1 is single-threaded): cppgc::Platform::PostJob runs
// kUserBlocking/kUserVisible on the posting thread before returning, and
// defers kBestEffort until JobHandle::Join() (or handle destruction).
class JobDelegate {
 public:
  virtual ~JobDelegate() = default;
  virtual bool ShouldYield() = 0;
  virtual void NotifyConcurrencyIncrease() = 0;
  virtual uint8_t GetTaskId() = 0;
  virtual bool IsJoiningThread() const = 0;
};

class JobHandle {
 public:
  virtual ~JobHandle() = default;
  virtual void NotifyConcurrencyIncrease() = 0;
  virtual void Join() = 0;
  virtual void Cancel() = 0;
  virtual void CancelAndDetach() = 0;
  virtual bool IsActive() = 0;
  virtual bool IsValid() = 0;
  virtual bool UpdatePriorityEnabled() const { return false; }
  virtual void UpdatePriority(TaskPriority new_priority) { (void)new_priority; }
};

class JobTask {
 public:
  virtual ~JobTask() = default;
  virtual void Run(JobDelegate* delegate) = 0;
  virtual size_t GetMaxConcurrency(size_t worker_count) const = 0;
};

class TracingController {
 public:
  virtual ~TracingController() = default;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_PLATFORM_H_
