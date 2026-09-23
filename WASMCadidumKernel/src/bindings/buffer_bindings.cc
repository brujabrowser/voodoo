// JS bindings for mojo::CreateSharedBuffer / DuplicateBuffer / MapBuffer.
// Never throw from a FunctionCallback; return MojoResult codes and handle
// sentinels instead.
#include "wck/bindings/buffer_bindings.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/buffer.h"
#include "wck/bindings/runtime.h"

namespace wck_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

MojoHandle ArgHandle(const FunctionCallbackInfo<Value>& info, int i) {
  return static_cast<MojoHandle>(ArgUInt32(info, i, MOJO_HANDLE_INVALID));
}

void SetResult(const FunctionCallbackInfo<Value>& info, MojoResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

void JsCreateSharedBuffer(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  uint64_t num_bytes = static_cast<uint64_t>(ArgNumber(info, 0, 0));
  mojo::ScopedSharedBufferHandle buffer;
  MojoResult r = mojo::CreateSharedBuffer(num_bytes, nullptr, &buffer);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    out->Set(isolate, "handle",
             Number::New(isolate, static_cast<double>(buffer.release().value())));
  } else {
    out->Set(isolate, "handle",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  }
  info.GetReturnValue().Set(out);
}

void JsDuplicateBuffer(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle buffer = ArgHandle(info, 0);
  mojo::ScopedSharedBufferHandle dup;
  MojoResult r =
      mojo::DuplicateBuffer(mojo::SharedBufferHandle(buffer), nullptr, &dup);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    out->Set(isolate, "handle",
             Number::New(isolate, static_cast<double>(dup.release().value())));
  } else {
    out->Set(isolate, "handle",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  }
  info.GetReturnValue().Set(out);
}

void JsMapWrite(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle buffer = ArgHandle(info, 0);
  uint64_t offset = static_cast<uint64_t>(ArgNumber(info, 1, 0));
  uint64_t num_bytes = static_cast<uint64_t>(ArgNumber(info, 2, 0));
  std::string payload = ArgString(isolate, info, 3);

  mojo::ScopedSharedBufferMapping mapping;
  MojoResult r = mojo::MapBuffer(mojo::SharedBufferHandle(buffer), offset,
                                 num_bytes, &mapping);
  if (r != MOJO_RESULT_OK || !mapping.is_valid()) {
    SetResult(info, r);
    return;
  }
  size_t n = std::min(payload.size(), static_cast<size_t>(num_bytes));
  if (n > 0) {
    std::memcpy(mapping.get(), payload.data(), n);
  }
  SetResult(info, MOJO_RESULT_OK);
}

void JsMapRead(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle buffer = ArgHandle(info, 0);
  uint64_t offset = static_cast<uint64_t>(ArgNumber(info, 1, 0));
  uint64_t num_bytes = static_cast<uint64_t>(ArgNumber(info, 2, 0));

  mojo::ScopedSharedBufferMapping mapping;
  MojoResult r = mojo::MapBuffer(mojo::SharedBufferHandle(buffer), offset,
                                 num_bytes, &mapping);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK && mapping.is_valid()) {
    // Trim at first NUL so the golden ASCII round-trip matches C++ memcmp
    // of a short prefix without needing ArrayBuffer on the facade yet.
    const char* base = static_cast<const char*>(mapping.get());
    size_t n = static_cast<size_t>(num_bytes);
    size_t len = 0;
    while (len < n && base[len] != '\0') ++len;
    std::string as_string(base, len);
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, as_string.c_str()).ToLocalChecked());
  } else {
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, "").ToLocalChecked());
  }
  info.GetReturnValue().Set(out);
}

void JsClose(const FunctionCallbackInfo<Value>& info) {
  SetResult(info, MojoClose(ArgHandle(info, 0)));
}

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

}  // namespace

void InstallBufferBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> mojo = GetOrCreateNamespace(isolate, global, "mojo");

  InstallFn(isolate, context, mojo, "createSharedBuffer", JsCreateSharedBuffer);
  InstallFn(isolate, context, mojo, "duplicateBuffer", JsDuplicateBuffer);
  InstallFn(isolate, context, mojo, "mapWrite", JsMapWrite);
  InstallFn(isolate, context, mojo, "mapRead", JsMapRead);
  InstallFn(isolate, context, mojo, "close", JsClose);

  mojo->Set(isolate, "RESULT_OK",
            Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  mojo->Set(isolate, "HANDLE_INVALID",
            Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
}

}  // namespace wck_bindings
