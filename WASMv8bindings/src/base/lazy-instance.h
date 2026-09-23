// Minimal stand-in for V8's src/base/lazy-instance.h -- just LeakyObject<T>,
// a process-lifetime singleton that is deliberately never destructed (real
// V8 uses this for the same GCInfoTable singleton; wasm32-wasip1 has no
// dlclose-style teardown to race with).
#ifndef WASMV8_SRC_BASE_LAZY_INSTANCE_H_
#define WASMV8_SRC_BASE_LAZY_INSTANCE_H_

#include <new>
#include <utility>

namespace v8::base {

template <typename T>
class LeakyObject {
 public:
  template <typename... Args>
  explicit LeakyObject(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  T* get() { return reinterpret_cast<T*>(storage_); }

 private:
  alignas(T) unsigned char storage_[sizeof(T)];
};

}  // namespace v8::base

#endif  // WASMV8_SRC_BASE_LAZY_INSTANCE_H_
