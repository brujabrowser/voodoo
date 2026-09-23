// Simplified from V8's include/v8-global.h -- Global<T>, a handle that
// outlives its creating scope (unlike Local<T>), the shape needed to hold
// e.g. a JS event-listener function past the call that registered it.
//
// Real V8's Global<T> exists because Local<T> handles die with their
// HandleScope; this facade's Local<T> is already a self-sufficient,
// scope-independent RAII handle (see v8-local-handle.h's file comment --
// it dups/frees quickjs's own refcount directly). So Global<T> here is
// just Local<T> wearing real V8's move-only, "must call Get()/Reset()
// through an Isolate" API shape -- there is no second handle
// representation underneath, unlike real V8 where Global<T> and Local<T>
// are genuinely different mechanisms. Not ported: SetWeak() (weak
// callbacks on a Global going unreachable -- this facade has no way to
// observe a JS value becoming unreachable except through Object::Wrap's
// finalizer path, which is a different mechanism).
#ifndef WASMV8_INCLUDE_V8_GLOBAL_H_
#define WASMV8_INCLUDE_V8_GLOBAL_H_

#include <utility>

#include "v8-local-handle.h"

namespace v8 {

class Isolate;

template <typename T>
class Global {
 public:
  Global() = default;
  Global(Isolate*, Local<T> value) : value_(std::move(value)) {}  // NOLINT

  Global(Global&& other) noexcept : value_(std::move(other.value_)) {
    other.value_.Clear();
  }
  Global& operator=(Global&& other) noexcept {
    if (this != &other) {
      value_ = std::move(other.value_);
      other.value_.Clear();
    }
    return *this;
  }
  Global(const Global&) = delete;
  Global& operator=(const Global&) = delete;

  bool IsEmpty() const { return value_.IsEmpty(); }
  void Reset() { value_.Clear(); }
  void Reset(Isolate*, Local<T> value) { value_ = std::move(value); }

  // Returns a fresh Local<T> (a new dup'd reference) usable however long
  // the caller likes -- real V8 ties this to the current HandleScope; this
  // facade's Local<T> doesn't need one (see file comment).
  Local<T> Get(Isolate*) const { return value_; }

 private:
  Local<T> value_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_GLOBAL_H_
