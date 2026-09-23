// Simplified from V8's include/v8-context.h.
//
// Context wraps one quickjs JSContext (a full JS realm/global-object) --
// unlike Value/Object/String/Function, a JSContext isn't a JSValue and
// isn't refcounted by quickjs itself, so Local<Context> can't reuse the
// generic dup/free Local<T> template from v8-local-handle.h. It's a full
// specialization instead: a small non-owning view over a JSContext* whose
// actual lifetime is owned by the Isolate that created it (freed on
// Isolate::Dispose()) -- see v8-isolate.h.
#ifndef WASMV8_INCLUDE_V8_CONTEXT_H_
#define WASMV8_INCLUDE_V8_CONTEXT_H_

#include "quickjs.h"
#include "v8-local-handle.h"

namespace v8 {

class Isolate;
class Object;

class Context final {
 public:
  Context() = default;
  explicit Context(JSContext* ctx) : ctx_(ctx) {}

  static Local<Context> New(Isolate* isolate);

  Isolate* GetIsolate() const;
  Local<Object> Global() const;

  JSContext* context_for_wasmv8_internal() const { return ctx_; }

  class Scope {
   public:
    explicit Scope(Local<Context> context);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    JSContext* previous_;
  };

 private:
  JSContext* ctx_ = nullptr;
};

template <>
class Local<Context> {
 public:
  Local() = default;
  explicit Local(JSContext* ctx) : ctx_(ctx) {}

  bool IsEmpty() const { return ctx_ == nullptr; }
  void Clear() { ctx_ = nullptr; }

  Context* operator->() const {
    view_ = Context(ctx_);
    return &view_;
  }
  Context& operator*() const {
    view_ = Context(ctx_);
    return view_;
  }

  JSContext* context_for_wasmv8_internal() const { return ctx_; }

 private:
  JSContext* ctx_ = nullptr;
  mutable Context view_;
};

// Returns the JSContext* a Context is currently entered on -- used
// throughout the facade wherever real V8 code would call
// isolate->GetCurrentContext().
JSContext* CurrentContext(Isolate* isolate);

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_CONTEXT_H_
