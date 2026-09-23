// JS bindings for mojo::CreateMessagePipe / WriteMessageRaw / ReadMessageRaw
 // and MojoClose. Calling convention matches WASMExtWrench's C-API tier:
 // never throw from a FunctionCallback (facade has no
 // Isolate::ThrowException yet); return MojoResult codes and handle
 // sentinels instead. See that file's comment for the full reasoning.
#include "wck/bindings/message_pipe_bindings.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/message_pipe.h"
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

void JsCreateMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  mojo::ScopedMessagePipeHandle a, b;
  MojoResult r = mojo::CreateMessagePipe(nullptr, &a, &b);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    // release() so the ScopedHandleBase destructors don't MojoClose the
    // handles before JS takes ownership -- JS must call mojo.close().
    out->Set(isolate, "handle0",
             Number::New(isolate, static_cast<double>(a.release().value())));
    out->Set(isolate, "handle1",
             Number::New(isolate, static_cast<double>(b.release().value())));
  } else {
    out->Set(isolate, "handle0",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
    out->Set(isolate, "handle1",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  }
  info.GetReturnValue().Set(out);
}

void JsWriteMessageRaw(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle pipe = ArgHandle(info, 0);
  std::string payload = ArgString(isolate, info, 1);
  SetResult(info,
            mojo::WriteMessageRaw(mojo::MessagePipeHandle(pipe), payload.data(),
                                  static_cast<uint32_t>(payload.size()),
                                  nullptr, 0, MOJO_WRITE_MESSAGE_FLAG_NONE));
}

void JsReadMessageRaw(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle pipe = ArgHandle(info, 0);
  std::vector<uint8_t> payload;
  std::vector<MojoHandle> handles;
  MojoResult r =
      mojo::ReadMessageRaw(mojo::MessagePipeHandle(pipe), &payload, &handles,
                           MOJO_READ_MESSAGE_FLAG_NONE);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    // Payload as a JS string (byte-identical for the ASCII round-trip the
    // golden test exercises). v8::Array isn't ported yet, so attached
    // handles land as a trailing `handleCount` rather than a real array --
    // same facade-gap adaptation ExtWrench's ProxyBuild uses for argv.
    std::string as_string(payload.begin(), payload.end());
    out->Set(isolate, "payload",
             String::NewFromUtf8(isolate, as_string.c_str()).ToLocalChecked());
    out->Set(isolate, "handleCount",
             Number::New(isolate, static_cast<double>(handles.size())));
    // Close any transferred handles the caller didn't ask for -- first
    // tier only covers byte payloads. Avoid leaking them into the process.
    for (MojoHandle h : handles) {
      if (h != MOJO_HANDLE_INVALID) MojoClose(h);
    }
  } else {
    out->Set(isolate, "payload", String::NewFromUtf8(isolate, "").ToLocalChecked());
    out->Set(isolate, "handleCount", Number::New(isolate, 0));
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
  Local<Object> mojo = GetOrCreateNamespace(isolate, global, "mojo");

  InstallFn(isolate, context, mojo, "createMessagePipe", JsCreateMessagePipe);
  InstallFn(isolate, context, mojo, "writeMessageRaw", JsWriteMessageRaw);
  InstallFn(isolate, context, mojo, "readMessageRaw", JsReadMessageRaw);
  InstallFn(isolate, context, mojo, "close", JsClose);

  mojo->Set(isolate, "RESULT_OK",
            Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  mojo->Set(isolate, "HANDLE_INVALID",
            Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
}

}  // namespace wck_bindings
