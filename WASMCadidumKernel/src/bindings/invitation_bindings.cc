// JS bindings for mojo::OutgoingInvitation / IncomingInvitation loopback.
// Handle-as-number over the C ABI; never throw from a FunctionCallback.
#include "wck/bindings/invitation_bindings.h"

#include <cstdint>
#include <string>

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/invitation.h"
#include "wck/bindings/runtime.h"

namespace wck_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::Value;

MojoHandle ArgHandle(const FunctionCallbackInfo<Value>& info, int i) {
  return static_cast<MojoHandle>(ArgUInt32(info, i, MOJO_HANDLE_INVALID));
}

void SetResult(const FunctionCallbackInfo<Value>& info, MojoResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

void JsCreateInvitation(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle handle = MOJO_HANDLE_INVALID;
  MojoResult r = MojoCreateInvitation(nullptr, &handle);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "handle",
           Number::New(isolate, static_cast<double>(
                                    r == MOJO_RESULT_OK ? handle
                                                       : MOJO_HANDLE_INVALID)));
  info.GetReturnValue().Set(out);
}

void JsAttachMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle invitation = ArgHandle(info, 0);
  std::string name = ArgString(isolate, info, 1);
  MojoHandle pipe = MOJO_HANDLE_INVALID;
  MojoResult r = MojoAttachMessagePipeToInvitation(
      invitation, name.data(), static_cast<uint32_t>(name.size()), nullptr,
      &pipe);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "handle",
           Number::New(isolate, static_cast<double>(
                                    r == MOJO_RESULT_OK ? pipe
                                                       : MOJO_HANDLE_INVALID)));
  info.GetReturnValue().Set(out);
}

void JsSendInvitation(const FunctionCallbackInfo<Value>& info) {
  MojoHandle invitation = ArgHandle(info, 0);
  uint64_t channel_id = static_cast<uint64_t>(ArgNumber(info, 1, 0));
  mojo::LoopbackChannel channel(channel_id);
  MojoInvitationTransportEndpoint endpoint = channel.AsEndpoint();
  // MojoSendInvitation consumes the invitation handle on success.
  SetResult(info, MojoSendInvitation(invitation, nullptr, &endpoint, nullptr, 0,
                                     nullptr));
}

void JsAcceptInvitation(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  uint64_t channel_id = static_cast<uint64_t>(ArgNumber(info, 0, 0));
  mojo::LoopbackChannel channel(channel_id);
  MojoInvitationTransportEndpoint endpoint = channel.AsEndpoint();
  MojoHandle handle = MOJO_HANDLE_INVALID;
  MojoResult r = MojoAcceptInvitation(&endpoint, nullptr, &handle);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "handle",
           Number::New(isolate, static_cast<double>(
                                    r == MOJO_RESULT_OK ? handle
                                                       : MOJO_HANDLE_INVALID)));
  info.GetReturnValue().Set(out);
}

void JsExtractMessagePipe(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  MojoHandle invitation = ArgHandle(info, 0);
  std::string name = ArgString(isolate, info, 1);
  MojoHandle pipe = MOJO_HANDLE_INVALID;
  MojoResult r = MojoExtractMessagePipeFromInvitation(
      invitation, name.data(), static_cast<uint32_t>(name.size()), nullptr,
      &pipe);

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "handle",
           Number::New(isolate, static_cast<double>(
                                    r == MOJO_RESULT_OK ? pipe
                                                       : MOJO_HANDLE_INVALID)));
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

void InstallInvitationBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> mojo = GetOrCreateNamespace(isolate, global, "mojo");

  InstallFn(isolate, context, mojo, "createInvitation", JsCreateInvitation);
  InstallFn(isolate, context, mojo, "attachMessagePipe", JsAttachMessagePipe);
  InstallFn(isolate, context, mojo, "sendInvitation", JsSendInvitation);
  InstallFn(isolate, context, mojo, "acceptInvitation", JsAcceptInvitation);
  InstallFn(isolate, context, mojo, "extractMessagePipe", JsExtractMessagePipe);
  InstallFn(isolate, context, mojo, "close", JsClose);

  mojo->Set(isolate, "RESULT_OK",
            Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  mojo->Set(isolate, "HANDLE_INVALID",
            Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
}

}  // namespace wck_bindings
