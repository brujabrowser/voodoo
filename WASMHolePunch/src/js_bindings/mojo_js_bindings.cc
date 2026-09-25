#include "whp/js_bindings/mojo_js_bindings.h"

#include <cstring>
#include <string>

#include "whp/c/system.h"
#include "whp/js_bindings/runtime.h"

namespace whp_js_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

void SetResult(const FunctionCallbackInfo<Value>& info, WhpResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

WhpHandle HandleValue(Isolate* isolate, Local<Object> handle) {
  v8::MaybeLocal<Value> maybe = handle->Get(isolate, "value");
  Local<Value> v;
  if (!maybe.ToLocal(&v) || !v->IsNumber()) return WHP_HANDLE_INVALID;
  return static_cast<WhpHandle>(v.As<Number>()->Value());
}

WhpHandle ArgHandleObject(const FunctionCallbackInfo<Value>& info, int i) {
  Isolate* isolate = info.GetIsolate();
  Local<Value> v = info[i];
  if (v->IsNumber()) return static_cast<WhpHandle>(v.As<Number>()->Value());
  if (!v->IsObject()) return WHP_HANDLE_INVALID;
  return HandleValue(isolate, v.As<Object>());
}

WhpResult WritePayload(WhpHandle pipe, const std::string& payload) {
  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpCreateMessage(nullptr, &msg);
  if (r != WHP_RESULT_OK) return r;
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  r = WhpAppendMessageData(msg, n, nullptr, 0, &opts, &buf, &sz);
  if (r != WHP_RESULT_OK) {
    WhpDestroyMessage(msg);
    return r;
  }
  if (n > 0) std::memcpy(buf, payload.data(), n);
  r = WhpWriteMessage(pipe, msg, nullptr);
  if (r != WHP_RESULT_OK) WhpDestroyMessage(msg);
  return r;
}

void JsReadMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle pipe = HandleValue(isolate, info.This());
  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpReadMessage(pipe, nullptr, &got);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == WHP_RESULT_OK) {
    void* rbuf = nullptr;
    uint32_t rn = 0;
    r = WhpGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr);
    out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
    std::string s = r == WHP_RESULT_OK ? std::string(static_cast<const char*>(rbuf), rn) : std::string();
    out->Set(isolate, "payload", String::NewFromUtf8(isolate, s.c_str()).ToLocalChecked());
    WhpDestroyMessage(got);
  } else {
    out->Set(isolate, "payload", String::NewFromUtf8(isolate, "").ToLocalChecked());
  }
  info.GetReturnValue().Set(out);
}

void JsWriteMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  SetResult(info, WritePayload(HandleValue(isolate, info.This()), ArgString(isolate, info, 0)));
}

void JsClose(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  SetResult(info, WhpClose(HandleValue(isolate, info.This())));
}

Local<Object> MakeHandle(Isolate* isolate, Local<v8::Context> context, WhpHandle handle) {
  Local<Object> obj = Object::New(isolate);
  obj->Set(isolate, "value", Number::New(isolate, static_cast<double>(handle)));
  InstallFn(isolate, context, obj, "readMessage", JsReadMessage);
  InstallFn(isolate, context, obj, "writeMessage", JsWriteMessage);
  InstallFn(isolate, context, obj, "close", JsClose);
  return obj;
}

void JsCreateMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  WhpResult r = WhpCreateMessagePipe(nullptr, &a, &b);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  Local<v8::Context> context(v8::CurrentContext(isolate));
  out->Set(isolate, "handle0", MakeHandle(isolate, context, a));
  out->Set(isolate, "handle1", MakeHandle(isolate, context, b));
  info.GetReturnValue().Set(out);
}

void JsBindInterface(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string name = ArgString(isolate, info, 0);
  WhpResult wrote = WritePayload(ArgHandleObject(info, 1), name);
  Local<v8::Context> context(v8::CurrentContext(isolate));
  Local<v8::Promise::Resolver> resolver =
      v8::Promise::Resolver::New(context).ToLocalChecked();
  resolver->Resolve(context, Number::New(isolate, static_cast<double>(wrote)));
  info.GetReturnValue().Set(resolver->GetPromise());
}

}  // namespace

void InstallMojoJs(Isolate* isolate, Local<v8::Context> context) {
  WhpInit();
  Local<Object> mojo = GetOrCreateNamespace(isolate, context->Global(), "Mojo");
  InstallFn(isolate, context, mojo, "createMessagePipe", JsCreateMessagePipe);
  InstallFn(isolate, context, mojo, "bindInterface", JsBindInterface);
  mojo->Set(isolate, "RESULT_OK", Number::New(isolate, static_cast<double>(WHP_RESULT_OK)));
  mojo->Set(isolate, "BROWSER_INTERFACE_BROKER", Number::New(isolate, 131441659.0));
}

}  // namespace whp_js_bindings
