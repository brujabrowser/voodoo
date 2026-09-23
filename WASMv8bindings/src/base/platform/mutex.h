// Minimal stand-in for V8's src/base/platform/mutex.h. wasm32-wasip1 is
// single-threaded (go++'s own Oilpan-lite design note applies here too), so
// Mutex/MutexGuard are no-ops that exist only so ported call sites (e.g.
// GCInfoTable::RegisterNewGCInfo) don't need editing.
#ifndef WASMV8_SRC_BASE_PLATFORM_MUTEX_H_
#define WASMV8_SRC_BASE_PLATFORM_MUTEX_H_

namespace v8::base {

class Mutex {
 public:
  void Lock() {}
  void Unlock() {}
};

class MutexGuard {
 public:
  explicit MutexGuard(Mutex* mutex) : mutex_(mutex) { mutex_->Lock(); }
  ~MutexGuard() { mutex_->Unlock(); }
  MutexGuard(const MutexGuard&) = delete;
  MutexGuard& operator=(const MutexGuard&) = delete;

 private:
  Mutex* mutex_;
};

}  // namespace v8::base

#endif  // WASMV8_SRC_BASE_PLATFORM_MUTEX_H_
