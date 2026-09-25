#include "v8-promise.h"

#include "v8-context.h"
#include "v8-exception.h"
#include "v8-isolate.h"
#include "v8-primitive.h"

namespace v8 {
namespace {

bool Settle(Object* resolver, Local<Context> context, Local<Value> value, const char* slot) {
  JSContext* ctx = context.context_for_wasmv8_internal();
  v8::MaybeLocal<Value> maybe = resolver->Get(context->GetIsolate(), slot);
  Local<Value> fn;
  if (!maybe.ToLocal(&fn) || !fn->IsFunction()) return false;
  JSValue arg = JS_DupValue(ctx, value.value_for_wasmv8_internal());
  JSValue ret = JS_Call(ctx, fn.value_for_wasmv8_internal(), JS_UNDEFINED, 1, &arg);
  JS_FreeValue(ctx, arg);
  if (JS_IsException(ret)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return false;
  }
  JS_FreeValue(ctx, ret);
  return true;
}

}  // namespace

MaybeLocal<Promise::Resolver> Promise::Resolver::New(Local<Context> context) {
  JSContext* ctx = context.context_for_wasmv8_internal();
  Isolate* isolate = context->GetIsolate();
  JSValue funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue promise = JS_NewPromiseCapability(ctx, funcs);
  if (JS_IsException(promise)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<Resolver>();
  }
  Local<Object> resolver = Object::New(isolate);
  resolver->Set(isolate, "promise", Local<Value>::Adopt(ctx, promise));
  resolver->Set(isolate, "resolve", Local<Value>::Adopt(ctx, funcs[0]));
  resolver->Set(isolate, "reject", Local<Value>::Adopt(ctx, funcs[1]));
  return MaybeLocal<Resolver>(resolver.As<Resolver>());
}

Local<Promise> Promise::Resolver::GetPromise() {
  Isolate* isolate = Context(ctx_).GetIsolate();
  v8::MaybeLocal<Value> maybe = Get(isolate, "promise");
  return maybe.ToLocalChecked().As<Promise>();
}

bool Promise::Resolver::Resolve(Local<Context> context, Local<Value> value) {
  return Settle(this, context, value, "resolve");
}

bool Promise::Resolver::Reject(Local<Context> context, Local<Value> value) {
  return Settle(this, context, value, "reject");
}

Promise::PromiseState Promise::State() const {
  switch (JS_PromiseState(ctx_, val_)) {
    case JS_PROMISE_FULFILLED:
      return kFulfilled;
    case JS_PROMISE_REJECTED:
      return kRejected;
    default:
      return kPending;
  }
}

Local<Value> Promise::Result() const {
  return Local<Value>::Adopt(ctx_, JS_PromiseResult(ctx_, val_));
}

void Isolate::PerformMicrotaskCheckpoint() {
  JSContext* ctx = nullptr;
  for (;;) {
    int ran = JS_ExecutePendingJob(rt_, &ctx);
    if (ran <= 0) break;
  }
}

}  // namespace v8
