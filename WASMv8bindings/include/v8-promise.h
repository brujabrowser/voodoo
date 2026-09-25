// v8::Promise::Resolver on quickjs JS_NewPromiseCapability.
// Resolve queues the reaction. Isolate::PerformMicrotaskCheckpoint runs it.
#ifndef WASMV8_INCLUDE_V8_PROMISE_H_
#define WASMV8_INCLUDE_V8_PROMISE_H_

#include "v8-maybe.h"
#include "v8-object.h"

namespace v8 {

class Context;
class Isolate;

class Promise : public Object {
 public:
  Promise() = default;
  Promise(JSContext* ctx, JSValue val) : Object(ctx, val) {}

  enum PromiseState { kPending, kFulfilled, kRejected };

  class Resolver : public Object {
   public:
    Resolver() = default;
    Resolver(JSContext* ctx, JSValue val) : Object(ctx, val) {}

    static MaybeLocal<Resolver> New(Local<Context> context);
    Local<Promise> GetPromise();
    bool Resolve(Local<Context> context, Local<Value> value);
    bool Reject(Local<Context> context, Local<Value> value);
  };

  PromiseState State() const;
  Local<Value> Result() const;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_PROMISE_H_
