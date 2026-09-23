// JS bindings for mojo::CreateDataPipe / WriteDataRaw / ReadDataRaw
// and MojoClose. Calling convention matches message_pipe_bindings.cc:
// never throw from a FunctionCallback; return MojoResult codes and
// handle sentinels instead.
#include "wck/bindings/data_pipe_bindings.h"

#include <cstdint>
#include <string>
#include <vector>

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/data_pipe.h"
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

void JsCreateDataPipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  MojoResult r = mojo::CreateDataPipe(nullptr, &producer, &consumer);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    // release() so ScopedHandleBase destructors don't MojoClose before
    // JS takes ownership -- JS must call mojo.close().
    out->Set(isolate, "producer",
             Number::New(isolate,
                         static_cast<double>(producer.release().value())));
    out->Set(isolate, "consumer",
             Number::New(isolate,
                         static_cast<double>(consumer.release().value())));
  } else {
    out->Set(isolate, "producer",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
    out->Set(isolate, "consumer",
             Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  }
  info.GetReturnValue().Set(out);
}

void JsWriteData(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle producer = ArgHandle(info, 0);
  std::string payload = ArgString(isolate, info, 1);
  uint32_t n = static_cast<uint32_t>(payload.size());
  SetResult(info,
            mojo::WriteDataRaw(mojo::DataPipeProducerHandle(producer),
                               payload.data(), &n, nullptr));
}

void JsReadData(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle consumer = ArgHandle(info, 0);
  int max_bytes = ArgInt(info, 1, 4096);
  if (max_bytes < 0) max_bytes = 0;
  uint32_t n = static_cast<uint32_t>(max_bytes);
  std::vector<char> buf(n);
  MojoResult r = mojo::ReadDataRaw(mojo::DataPipeConsumerHandle(consumer),
                                   buf.data(), &n, nullptr);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  if (r == MOJO_RESULT_OK) {
    // Payload as a JS string (byte-identical for the ASCII round-trip the
    // golden test exercises), same as message_pipe_bindings.
    std::string as_string(buf.data(), n);
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

void InstallDataPipeBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> mojo = GetOrCreateNamespace(isolate, global, "mojo");

  InstallFn(isolate, context, mojo, "createDataPipe", JsCreateDataPipe);
  InstallFn(isolate, context, mojo, "writeData", JsWriteData);
  InstallFn(isolate, context, mojo, "readData", JsReadData);
  // Same close as message_pipe; GetOrCreateNamespace lets either tier
  // install it (or both -- last Install wins, same MojoClose).
  InstallFn(isolate, context, mojo, "close", JsClose);

  mojo->Set(isolate, "RESULT_OK",
            Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  mojo->Set(isolate, "HANDLE_INVALID",
            Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
}

}  // namespace wck_bindings
