// Simplified from V8's include/v8-function-callback.h -- FunctionCallback,
// FunctionCallbackInfo<T>, ReturnValue<T>, matching real V8's shape closely
// enough that a callback body reads the same
// (`info[0]`, `info.This()`, `info.GetReturnValue().Set(...)`) whether it's
// running against real V8 or this facade. Not ported: NewTarget()/IsConstructCall()
// (no constructor-call support yet -- see v8-template.h), Data() (no
// per-template embedder data slot yet).
#ifndef WASMV8_INCLUDE_V8_FUNCTION_CALLBACK_H_
#define WASMV8_INCLUDE_V8_FUNCTION_CALLBACK_H_

#include "quickjs.h"
#include "v8-local-handle.h"
#include "v8-object.h"
#include "v8-value.h"

namespace v8 {

class Isolate;

template <typename T>
class ReturnValue {
 public:
  ReturnValue(JSContext* ctx, JSValue* out) : ctx_(ctx), out_(out) {}

  void Set(Local<Value> value) {
    *out_ = JS_DupValue(ctx_, value.value_for_wasmv8_internal());
  }
  void SetUndefined() { *out_ = JS_UNDEFINED; }
  void SetNull() { *out_ = JS_NULL; }
  void Set(bool value) { *out_ = JS_NewBool(ctx_, value); }
  void Set(double value) { *out_ = JS_NewFloat64(ctx_, value); }
  void Set(int32_t value) { *out_ = JS_NewInt32(ctx_, value); }

 private:
  JSContext* ctx_;
  JSValue* out_;
};

template <typename T>
class FunctionCallbackInfo {
 public:
  FunctionCallbackInfo(Isolate* isolate, JSContext* ctx, JSValue this_val,
                       int argc, JSValue* argv, JSValue* return_value)
      : isolate_(isolate),
        ctx_(ctx),
        this_val_(this_val),
        argc_(argc),
        argv_(argv),
        return_value_(return_value) {}

  int Length() const { return argc_; }

  Local<Value> operator[](int i) const {
    if (i < 0 || i >= argc_) return Local<Value>::Adopt(ctx_, JS_UNDEFINED);
    return Local<Value>::Adopt(ctx_, JS_DupValue(ctx_, argv_[i]));
  }

  Local<Object> This() const {
    return Local<Object>::Adopt(ctx_, JS_DupValue(ctx_, this_val_));
  }

  Isolate* GetIsolate() const { return isolate_; }
  ReturnValue<T> GetReturnValue() const { return ReturnValue<T>(ctx_, return_value_); }

  JSContext* context_for_wasmv8_internal() const { return ctx_; }
  JSValue this_for_wasmv8_internal() const { return this_val_; }

 private:
  Isolate* isolate_;
  JSContext* ctx_;
  JSValue this_val_;
  int argc_;
  JSValue* argv_;
  JSValue* return_value_;
};

using FunctionCallback = void (*)(const FunctionCallbackInfo<Value>& info);

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_FUNCTION_CALLBACK_H_
