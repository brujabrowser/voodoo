// JS bindings for MojoCreateMessagePipe / MojoWriteMessage /
// MojoReadMessage and MojoClose. Calling convention matches
// WASMCadidumKernel's C++-API tier: never throw from a FunctionCallback
// (facade has no Isolate::ThrowException yet); return MojoResult codes and
// handle sentinels instead. writeMessage / readMessage build the Mojo C
// message path (CreateMessage + AppendMessageData + Write / Read +
// GetMessageData) the same way tests/core/message_pipe_roundtrip.cc does.
#include "wst/bindings/message_pipe_bindings.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "mojo/public/c/system/core.h"
#include "wst/bindings/runtime.h"

namespace wst_bindings {
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
  return static_cast<MojoHandle>(ArgInt(info, i, MOJO_HANDLE_INVALID));
}

void SetResult(const FunctionCallbackInfo<Value>& info, MojoResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

void JsCreateMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  MojoResult r = MojoCreateMessagePipe(nullptr, &a, &b);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  // Handles are opaque uint32_t numbers -- JS owns them and must call
  // wst.close(). No ScopedHandle / cppgc wrap at this tier.
  if (r == MOJO_RESULT_OK) {
    out->Set(isolate, "handle0", Number::New(isolate, static_cast<double>(a)));
    out->Set(isolate, "handle1", Number::New(isolate, static_cast<double>(b)));
  } else {
    out->Set(isolate, "handle0",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
    out->Set(isolate, "handle1",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  }
  info.GetReturnValue().Set(out);
}

void JsWriteMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle pipe = ArgHandle(info, 0);
  std::string payload = ArgString(isolate, info, 1);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  MojoResult r = MojoCreateMessage(nullptr, &msg);
  if (r != MOJO_RESULT_OK) {
    SetResult(info, r);
    return;
  }

  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  r = MojoAppendMessageData(msg, n, nullptr, 0, &opts, &buf, &sz);
  if (r != MOJO_RESULT_OK) {
    MojoDestroyMessage(msg);
    SetResult(info, r);
    return;
  }
  if (n > 0) {
    std::memcpy(buf, payload.data(), n);
  }

  // MojoWriteMessage takes ownership of `msg` on success.
  r = MojoWriteMessage(pipe, msg, nullptr);
  if (r != MOJO_RESULT_OK) {
    MojoDestroyMessage(msg);
  }
  SetResult(info, r);
}

void JsReadMessage(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle pipe = ArgHandle(info, 0);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  MojoResult r = MojoReadMessage(pipe, nullptr, &got);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    void* rbuf = nullptr;
    uint32_t rn = 0;
    r = MojoGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr);
    out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
    if (r == MOJO_RESULT_OK) {
      // Payload as a JS string (byte-identical for the ASCII round-trip the
      // golden test exercises).
      std::string as_string(static_cast<const char*>(rbuf), rn);
      out->Set(isolate, "payload",
               String::NewFromUtf8(isolate, as_string.c_str()).ToLocalChecked());
    } else {
      out->Set(isolate, "payload",
               String::NewFromUtf8(isolate, "").ToLocalChecked());
    }
    MojoDestroyMessage(got);
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

void InstallMessagePipeBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> wst = GetOrCreateNamespace(isolate, global, "wst");

  InstallFn(isolate, context, wst, "createMessagePipe", JsCreateMessagePipe);
  InstallFn(isolate, context, wst, "writeMessage", JsWriteMessage);
  InstallFn(isolate, context, wst, "readMessage", JsReadMessage);
  InstallFn(isolate, context, wst, "close", JsClose);

  wst->Set(isolate, "RESULT_OK",
           Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  wst->Set(isolate, "HANDLE_INVALID",
           Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
}

}  // namespace wst_bindings
