#include "wst/bindings/runtime.h"

namespace wst_bindings {

v8::Local<v8::Object> GetOrCreateNamespace(v8::Isolate* isolate,
                                           v8::Local<v8::Object> parent,
                                           const char* name) {
  v8::MaybeLocal<v8::Value> maybe = parent->Get(isolate, name);
  v8::Local<v8::Value> existing;
  if (maybe.ToLocal(&existing) && existing->IsObject()) {
    return existing.As<v8::Object>();
  }
  v8::Local<v8::Object> fresh = v8::Object::New(isolate);
  parent->Set(isolate, name, fresh);
  return fresh;
}

double ArgNumber(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
                 double default_value) {
  v8::Local<v8::Value> v = info[i];
  if (!v->IsNumber()) return default_value;
  return v.As<v8::Number>()->Value();
}

int ArgInt(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
           int default_value) {
  v8::Local<v8::Value> v = info[i];
  if (!v->IsNumber()) return default_value;
  return static_cast<int>(v.As<v8::Number>()->Value());
}

std::optional<std::string> ArgOptString(
    v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& info,
    int i) {
  v8::Local<v8::Value> v = info[i];
  if (v->IsNullOrUndefined()) return std::nullopt;
  v8::String::Utf8Value s(isolate, v);
  return std::string(*s, static_cast<size_t>(s.length()));
}

std::string ArgString(v8::Isolate* isolate,
                      const v8::FunctionCallbackInfo<v8::Value>& info, int i) {
  return ArgOptString(isolate, info, i).value_or(std::string());
}

}  // namespace wst_bindings
