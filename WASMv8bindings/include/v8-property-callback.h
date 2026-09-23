// Simplified from V8's include/v8-property-callback.h -- PropertyCallbackInfo<T>
// plus AccessorGetterCallback/AccessorSetterCallback, backing
// ObjectTemplate::SetAccessor (v8-template.h). Matches real V8's Web-IDL-attribute
// use case directly: brujac's `[readonly] attribute Type name;` and
// read-write attributes are exactly what this exists for. Not ported:
// interceptors (NamedPropertyHandlerConfiguration et al.), the
// PropertyAttribute/AccessControl flag arguments real SetAccessor also
// takes.
#ifndef WASMV8_INCLUDE_V8_PROPERTY_CALLBACK_H_
#define WASMV8_INCLUDE_V8_PROPERTY_CALLBACK_H_

#include "quickjs.h"
#include "v8-function-callback.h"
#include "v8-local-handle.h"
#include "v8-object.h"

namespace v8 {

class Isolate;
class String;

template <typename T>
class PropertyCallbackInfo {
 public:
  PropertyCallbackInfo(Isolate* isolate, JSContext* ctx, JSValue this_val,
                       JSValue* return_value = nullptr)
      : isolate_(isolate), ctx_(ctx), this_val_(this_val), return_value_(return_value) {}

  Isolate* GetIsolate() const { return isolate_; }
  Local<Object> This() const {
    return Local<Object>::Adopt(ctx_, JS_DupValue(ctx_, this_val_));
  }
  ReturnValue<T> GetReturnValue() const { return ReturnValue<T>(ctx_, return_value_); }

 private:
  Isolate* isolate_;
  JSContext* ctx_;
  JSValue this_val_;
  JSValue* return_value_;
};

using AccessorGetterCallback =
    void (*)(Local<String> property, const PropertyCallbackInfo<Value>& info);
using AccessorSetterCallback = void (*)(Local<String> property, Local<Value> value,
                                        const PropertyCallbackInfo<void>& info);

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_PROPERTY_CALLBACK_H_
