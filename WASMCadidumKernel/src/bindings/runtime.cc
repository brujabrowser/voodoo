#include "wck/bindings/runtime.h"

#include <cstdint>

namespace wck_bindings {

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

uint32_t ArgUInt32(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
                   uint32_t default_value) {
  v8::Local<v8::Value> v = info[i];
  if (!v->IsNumber()) return default_value;
  // Truncate toward zero into the uint32 range; invitation handles live at
  // 0xF0000000+ and must not pass through signed int.
  double d = v.As<v8::Number>()->Value();
  if (d < 0) return default_value;
  if (d > static_cast<double>(UINT32_MAX)) return default_value;
  return static_cast<uint32_t>(d);
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

}  // namespace wck_bindings
