// JS bindings for whp/c/system.h — message pipes + data pipes on wasmv8_facade.
// Lives under js_bindings/ so it does not collide with this repo's existing
// Mojo C++ message codec in src/bindings/.
#include "whp/js_bindings/system_bindings.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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

WhpHandle ArgHandle(const FunctionCallbackInfo<Value>& info, int i) {
  return static_cast<WhpHandle>(ArgInt(info, i, WHP_HANDLE_INVALID));
}

void SetResult(const FunctionCallbackInfo<Value>& info, WhpResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

void JsInit(const FunctionCallbackInfo<Value>& info) {
  SetResult(info, WhpInit());
}

void JsShutdown(const FunctionCallbackInfo<Value>& info) {
  WhpShutdown();
  info.GetReturnValue().SetUndefined();
}

void JsCreateMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  WhpResult r = WhpCreateMessagePipe(nullptr, &a, &b);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "handle0", Number::New(isolate, static_cast<double>(a)));
  out->Set(isolate, "handle1", Number::New(isolate, static_cast<double>(b)));
  info.GetReturnValue().Set(out);
}

void JsWriteMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle pipe = ArgHandle(info, 0);
  std::string payload = ArgString(isolate, info, 1);

  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpCreateMessage(nullptr, &msg);
  if (r != WHP_RESULT_OK) {
    SetResult(info, r);
    return;
  }
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  r = WhpAppendMessageData(msg, n, nullptr, 0, &opts, &buf, &sz);
  if (r != WHP_RESULT_OK) {
    WhpDestroyMessage(msg);
    SetResult(info, r);
    return;
  }
  if (n > 0) std::memcpy(buf, payload.data(), n);
  r = WhpWriteMessage(pipe, msg, nullptr);
  if (r != WHP_RESULT_OK) WhpDestroyMessage(msg);
  SetResult(info, r);
}

void JsReadMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle pipe = ArgHandle(info, 0);
  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpReadMessage(pipe, nullptr, &got);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == WHP_RESULT_OK) {
    void* rbuf = nullptr;
    uint32_t rn = 0;
    r = WhpGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr);
    out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
    if (r == WHP_RESULT_OK) {
      std::string s(static_cast<const char*>(rbuf), rn);
      out->Set(isolate, "payload",
               String::NewFromUtf8(isolate, s.c_str()).ToLocalChecked());
    } else {
      out->Set(isolate, "payload",
               String::NewFromUtf8(isolate, "").ToLocalChecked());
    }
    WhpDestroyMessage(got);
  } else {
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, "").ToLocalChecked());
  }
  info.GetReturnValue().Set(out);
}

void JsCreateDataPipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle prod = WHP_HANDLE_INVALID;
  WhpHandle cons = WHP_HANDLE_INVALID;
  WhpResult r = WhpCreateDataPipe(nullptr, &prod, &cons);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "producer", Number::New(isolate, static_cast<double>(prod)));
  out->Set(isolate, "consumer", Number::New(isolate, static_cast<double>(cons)));
  info.GetReturnValue().Set(out);
}

void JsWriteData(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle producer = ArgHandle(info, 0);
  std::string payload = ArgString(isolate, info, 1);
  uint32_t n = static_cast<uint32_t>(payload.size());
  SetResult(info, WhpWriteData(producer, payload.data(), &n, 0));
}

void JsReadData(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  WhpHandle consumer = ArgHandle(info, 0);
  int max_bytes = ArgInt(info, 1, 4096);
  if (max_bytes < 0) max_bytes = 0;
  uint32_t n = static_cast<uint32_t>(max_bytes);
  std::vector<char> buf(n ? n : 1);
  WhpResult r = WhpReadData(consumer, buf.data(), &n, 0);
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == WHP_RESULT_OK) {
    std::string s(buf.data(), n);
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, s.c_str()).ToLocalChecked());
  } else {
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, "").ToLocalChecked());
  }
  info.GetReturnValue().Set(out);
}

void JsClose(const FunctionCallbackInfo<Value>& info) {
  SetResult(info, WhpClose(ArgHandle(info, 0)));
}

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

}  // namespace

void InstallSystemBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> whp = GetOrCreateNamespace(isolate, global, "whp");
  InstallFn(isolate, context, whp, "init", JsInit);
  InstallFn(isolate, context, whp, "shutdown", JsShutdown);
  InstallFn(isolate, context, whp, "createMessagePipe", JsCreateMessagePipe);
  InstallFn(isolate, context, whp, "writeMessage", JsWriteMessage);
  InstallFn(isolate, context, whp, "readMessage", JsReadMessage);
  InstallFn(isolate, context, whp, "createDataPipe", JsCreateDataPipe);
  InstallFn(isolate, context, whp, "writeData", JsWriteData);
  InstallFn(isolate, context, whp, "readData", JsReadData);
  InstallFn(isolate, context, whp, "close", JsClose);
  whp->Set(isolate, "RESULT_OK",
           Number::New(isolate, static_cast<double>(WHP_RESULT_OK)));
  whp->Set(isolate, "HANDLE_INVALID",
           Number::New(isolate, static_cast<double>(WHP_HANDLE_INVALID)));
}

}  // namespace whp_js_bindings
