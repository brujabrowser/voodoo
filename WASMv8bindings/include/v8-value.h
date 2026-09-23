// Simplified from V8's include/v8-value.h. Real V8's Value hierarchy is
// deep (Primitive/Name/Symbol/BigInt/typed arrays/...); this facade covers
// only what the rest of this port implements -- Object, String, Number,
// Boolean, Function (each in their own header) -- and IsXxx() predicates
// for those. Add more as this port's own surface grows.
#ifndef WASMV8_INCLUDE_V8_VALUE_H_
#define WASMV8_INCLUDE_V8_VALUE_H_

#include "quickjs.h"

namespace v8 {

class Value {
 public:
  Value() : ctx_(nullptr), val_(JS_UNDEFINED) {}
  Value(JSContext* ctx, JSValue val) : ctx_(ctx), val_(val) {}

  bool IsUndefined() const { return JS_IsUndefined(val_); }
  bool IsNull() const { return JS_IsNull(val_); }
  bool IsNullOrUndefined() const { return IsNull() || IsUndefined(); }
  bool IsObject() const { return JS_IsObject(val_); }
  bool IsString() const { return JS_IsString(val_); }
  bool IsNumber() const { return JS_IsNumber(val_); }
  bool IsBoolean() const { return JS_IsBool(val_); }
  bool IsFunction() const { return ctx_ && JS_IsFunction(ctx_, val_); }

  JSContext* context_for_wasmv8_internal() const { return ctx_; }
  JSValue value_for_wasmv8_internal() const { return val_; }

 protected:
  JSContext* ctx_;
  JSValue val_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_VALUE_H_
