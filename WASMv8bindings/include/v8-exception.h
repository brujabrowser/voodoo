// Simplified from V8's include/v8-exception.h -- TryCatch, matching real
// V8's stack-scoped exception-capture idiom
// (`v8::TryCatch try_catch(isolate); ...; if (try_catch.HasCaught()) {...}`).
// Previously, every fallible call in this facade (Script::Run,
// Function::Call) that hit a JS exception just discarded it
// (`JS_FreeValue(ctx, JS_GetException(ctx))`) and returned an empty
// MaybeLocal -- correct as far as "did this fail" but with no way to see
// *why*. TryCatch is the real fix: while one is active on the current
// isolate, ReportException (called from every one of those call sites
// instead of discarding directly) captures the exception into it instead.
// Not ported: Message()/StackTrace() (structured error location info --
// this port surfaces only the raw thrown value), rethrow, terminate-on-error.
#ifndef WASMV8_INCLUDE_V8_EXCEPTION_H_
#define WASMV8_INCLUDE_V8_EXCEPTION_H_

#include "quickjs.h"
#include "v8-local-handle.h"
#include "v8-value.h"

namespace v8 {

class Isolate;

class TryCatch final {
 public:
  explicit TryCatch(Isolate* isolate);
  ~TryCatch();
  TryCatch(const TryCatch&) = delete;
  TryCatch& operator=(const TryCatch&) = delete;

  bool HasCaught() const { return has_caught_; }
  Local<Value> Exception() const { return exception_; }

  // Internal: called from Script::Run/Function::Call (and anything else
  // that would otherwise just discard a JS exception) after `ctx` reports
  // one. Captures it into the innermost active TryCatch (this facade uses
  // one thread-wide stack rather than a per-isolate one -- the same
  // simplification Context::Scope already makes, see v8-context.h), if
  // any; otherwise the exception is simply dropped, matching this
  // facade's prior behavior when nothing was watching.
  static void ReportException_for_wasmv8_internal(JSContext* ctx);

 private:
  Isolate* isolate_;
  TryCatch* previous_;
  bool has_caught_ = false;
  Local<Value> exception_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_EXCEPTION_H_
