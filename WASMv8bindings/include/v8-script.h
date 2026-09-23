// Simplified from V8's include/v8-script.h.
//
// Real V8 (and real quickjs, via JS_EVAL_FLAG_COMPILE_ONLY + JS_EvalFunction)
// support a genuine two-phase compile-then-run split, letting a compiled
// script be cached and run multiple times. This facade's Compile() just
// stashes the source string; Run() is where JS_Eval actually parses and
// executes it, every time. Real compile-once/run-many caching is a
// plausible future upgrade (swap Run()'s JS_Eval for JS_EvalFunction over
// a JS_EVAL_FLAG_COMPILE_ONLY result stored at Compile() time) that
// wouldn't change this class's public shape at all.
//
// Like Template (v8-template.h) and Context (v8-context.h), Script isn't
// backed by a JSValue, so Local<Script> is a shared_ptr-backed
// specialization rather than the generic dup/free Local<T>.
#ifndef WASMV8_INCLUDE_V8_SCRIPT_H_
#define WASMV8_INCLUDE_V8_SCRIPT_H_

#include <memory>
#include <string>

#include "v8-context.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-primitive.h"
#include "v8-value.h"

namespace v8 {

class Script final {
 public:
  static MaybeLocal<Script> Compile(Local<Context> context,
                                    Local<String> source);

  MaybeLocal<Value> Run(Local<Context> context);

 private:
  std::string source_;

  friend class Local<Script>;
};

template <>
class Local<Script> {
 public:
  Local() = default;

  bool IsEmpty() const { return !impl_; }
  void Clear() { impl_.reset(); }

  Script* operator->() const { return impl_.get(); }
  Script& operator*() const { return *impl_; }

  static Local<Script> Adopt(std::shared_ptr<Script> impl) {
    Local<Script> local;
    local.impl_ = std::move(impl);
    return local;
  }

 private:
  std::shared_ptr<Script> impl_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_SCRIPT_H_
