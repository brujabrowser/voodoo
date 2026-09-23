// Simplified from V8's include/v8-local-handle.h.
//
// Real V8's Local<T> is a raw pointer into a slot owned by the innermost
// active HandleScope; the scope's destructor bulk-invalidates every Local
// created inside it, and creating a Local<T> with no HandleScope on the
// stack is undefined behavior. That design exists because V8's own GC
// needs a cheap, GC-visitable place to find every live JS handle on the
// C++ stack.
//
// This facade's Local<Value>/Object/... are backed by quickjs JSValues,
// and quickjs already refcounts JSValue itself (JS_DupValue/JS_FreeValue).
// So here, Local<T> is a small self-sufficient RAII handle -- it dups on
// copy and frees on destruction -- and does NOT depend on a HandleScope
// for correctness. HandleScope (declared at the bottom of this file) is
// still provided, and real-V8-shaped code should still open one, both so
// code written against real V8 semantics behaves the same way here, and
// because a future upgrade to a non-refcounted handle representation
// could make it load-bearing -- but forgetting one is not a
// use-after-free here the way it would be in real V8, only a style
// deviation. This is a deliberate, documented simplification, not an
// oversight.
#ifndef WASMV8_INCLUDE_V8_LOCAL_HANDLE_H_
#define WASMV8_INCLUDE_V8_LOCAL_HANDLE_H_

#include <type_traits>
#include <utility>

#include "quickjs.h"

namespace v8 {

class Isolate;

template <typename T>
class Local {
 public:
  Local() : ctx_(nullptr), val_(JS_UNDEFINED) {}

  Local(const Local& other) : ctx_(other.ctx_), val_(DupOf(other)) {}
  Local(Local&& other) noexcept : ctx_(other.ctx_), val_(other.val_) {
    other.ctx_ = nullptr;
    other.val_ = JS_UNDEFINED;
  }

  // Upcast: Local<Object> -> Local<Value>, Local<Function> -> Local<Object>,
  // etc. -- mirrors real V8's implicit Local<T>-to-Local<Base> conversion.
  template <typename S, typename = std::enable_if_t<std::is_base_of_v<T, S>>>
  Local(const Local<S>& other)  // NOLINT(runtime/explicit)
      : ctx_(other.ctx_), val_(DupOf(other)) {}

  ~Local() { Reset(); }

  Local& operator=(const Local& other) {
    if (this != &other) {
      Reset();
      ctx_ = other.ctx_;
      val_ = DupOf(other);
    }
    return *this;
  }
  Local& operator=(Local&& other) noexcept {
    if (this != &other) {
      Reset();
      ctx_ = other.ctx_;
      val_ = other.val_;
      other.ctx_ = nullptr;
      other.val_ = JS_UNDEFINED;
    }
    return *this;
  }

  bool IsEmpty() const { return ctx_ == nullptr; }
  void Clear() { Reset(); }

  T* operator->() const {
    view_ = T(ctx_, val_);
    return &view_;
  }
  T& operator*() const {
    view_ = T(ctx_, val_);
    return view_;
  }

  // Internal: takes ownership of `owned_val` (no dup) -- the common case
  // when a facade .cc file just created a fresh JSValue via a JS_New*()
  // call and wants to hand it to the caller as a Local without an extra
  // dup+free round trip. Not part of real V8's public API surface, but not
  // hidden either -- see this file's header comment on why this facade
  // doesn't chase real V8's internal-visibility discipline as hard as its
  // memory-safety guarantees.
  static Local<T> Adopt(JSContext* ctx, JSValue owned_val) {
    Local<T> local;
    local.ctx_ = ctx;
    local.val_ = owned_val;
    return local;
  }

  JSContext* context_for_wasmv8_internal() const { return ctx_; }
  JSValue value_for_wasmv8_internal() const { return val_; }

  // Unchecked cast, matching real V8's Local<T>::As<S>() idiom -- e.g.
  // turning a Local<Value> known (by the caller) to hold a number into a
  // Local<Number>. No runtime type check; misuse just means the returned
  // handle's methods will misinterpret the underlying JSValue, exactly as
  // real V8's own .As<>() warns.
  template <typename S>
  Local<S> As() const {
    return Local<S>::Adopt(ctx_, DupOf(*this));
  }

 private:
  void Reset() {
    if (ctx_) JS_FreeValue(ctx_, val_);
    ctx_ = nullptr;
    val_ = JS_UNDEFINED;
  }
  template <typename S>
  static JSValue DupOf(const Local<S>& other) {
    return other.ctx_ ? JS_DupValue(other.ctx_, other.val_) : JS_UNDEFINED;
  }

  JSContext* ctx_;
  JSValue val_;
  mutable T view_;

  template <typename U>
  friend class Local;
};

template <typename T>
class MaybeLocal {
 public:
  MaybeLocal() = default;
  MaybeLocal(Local<T> local) : local_(std::move(local)) {}  // NOLINT

  bool IsEmpty() const { return local_.IsEmpty(); }

  Local<T> ToLocalChecked() const { return local_; }
  bool ToLocal(Local<T>* out) const {
    if (IsEmpty()) return false;
    *out = local_;
    return true;
  }

 private:
  Local<T> local_;
};

// See this file's header comment: real memory safety here comes from each
// Local<T>'s own quickjs refcount, not from this scope. Present for
// real-V8-shaped call sites and as a hook point for a future non-refcounted
// handle representation.
class HandleScope {
 public:
  explicit HandleScope(Isolate*) {}
  HandleScope(const HandleScope&) = delete;
  HandleScope& operator=(const HandleScope&) = delete;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_LOCAL_HANDLE_H_
