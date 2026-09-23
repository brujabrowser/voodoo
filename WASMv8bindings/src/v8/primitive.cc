#include "v8-primitive.h"

#include "quickjs.h"
#include "v8-context.h"
#include "v8-exception.h"
#include "v8-isolate.h"

namespace v8 {

MaybeLocal<String> String::NewFromUtf8(Isolate* isolate, const char* data) {
  JSContext* ctx = CurrentContext(isolate);
  JSValue val = JS_NewString(ctx, data);
  if (JS_IsException(val)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<String>();
  }
  return Local<String>::Adopt(ctx, val);
}

String::Utf8Value::Utf8Value(Isolate* isolate, Local<Value> value) {
  ctx_ = value.context_for_wasmv8_internal();
  if (!ctx_) ctx_ = CurrentContext(isolate);
  if (!ctx_) return;
  str_ = JS_ToCStringLen(ctx_, &len_, value.value_for_wasmv8_internal());
}

String::Utf8Value::~Utf8Value() {
  if (str_) JS_FreeCString(ctx_, str_);
}

Local<Number> Number::New(Isolate* isolate, double value) {
  JSContext* ctx = CurrentContext(isolate);
  return Local<Number>::Adopt(ctx, JS_NewFloat64(ctx, value));
}

double Number::Value() const {
  double d = 0;
  JS_ToFloat64(ctx_, &d, val_);
  return d;
}

Local<Boolean> Boolean::New(Isolate* isolate, bool value) {
  JSContext* ctx = CurrentContext(isolate);
  return Local<Boolean>::Adopt(ctx, JS_NewBool(ctx, value));
}

}  // namespace v8
