// In-process EchoString round-trip exposed as `wcb.echo(s)`. Builds a real
// Receiver/Remote pair, issues EchoString, pumps whp::Executor until the
 // response arrives, and returns the response string -- the same shape
 // tests/bindings/remote_receiver_roundtrip.cc exercises from C++, now
 // reachable from JS on wasmv8_facade.
#include "wcb/bindings/echo_bindings.h"

#include <functional>
#include <string>
#include <vector>

#include "echo_interface.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "wcb/bindings/runtime.h"
#include "whp/base/executor.h"

namespace wcb_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

class EchoImpl : public echo::Echo {
 public:
  void EchoString(const std::string& in,
                  base::OnceCallback<void(std::string)> callback) override {
    callback(in);
    if (listener_.is_bound()) {
      listener_->OnEcho("pushed:" + in);
    }
  }
  void SetListener(
      mojo::PendingAssociatedRemote<echo::EchoListener> listener) override {
    listener_.Bind(std::move(listener));
  }

 private:
  mojo::AssociatedRemote<echo::EchoListener> listener_;
};

class ListenerImpl : public echo::EchoListener {
 public:
  void OnEcho(const std::string& value) override { seen.push_back(value); }
  std::vector<std::string> seen;
};

void Pump(int iterations = 8) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

void JsEcho(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string in = ArgString(isolate, info, 0);

  EchoImpl impl;
  mojo::Receiver<echo::Echo> receiver(&impl);
  mojo::Remote<echo::Echo> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  std::string got;
  bool responded = false;
  remote->EchoString(in, [&](std::string out) {
    got = std::move(out);
    responded = true;
  });

  for (int i = 0; i < 8 && !responded; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }

  if (!responded) {
    info.GetReturnValue().Set(String::NewFromUtf8(isolate, "").ToLocalChecked());
    return;
  }
  info.GetReturnValue().Set(
      String::NewFromUtf8(isolate, got.c_str()).ToLocalChecked());
}

// Associated-interface round-trip: SetListener + EchoString pushes OnEcho.
// Returns `{ echo, notification }` (empty strings on failure).
void JsEchoWithListener(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string in = ArgString(isolate, info, 0);

  EchoImpl echo_impl;
  mojo::Receiver<echo::Echo> echo_receiver(&echo_impl);
  mojo::Remote<echo::Echo> echo_remote;
  echo_receiver.Bind(echo_remote.BindNewPipeAndPassReceiver());

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);
  {
    mojo::AssociatedGroup group = echo_remote.associated_group();
    mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote;
    mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver =
        listener_remote.InitWithNewEndpointAndPassReceiver(group);
    echo_remote->SetListener(std::move(listener_remote));
    listener_receiver_obj.Bind(std::move(listener_receiver));
  }
  Pump();

  std::string got;
  bool responded = false;
  echo_remote->EchoString(in, [&](std::string out) {
    got = std::move(out);
    responded = true;
  });
  Pump();

  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "echo",
           String::NewFromUtf8(isolate, responded ? got.c_str() : "")
               .ToLocalChecked());
  out->Set(isolate, "notification",
           String::NewFromUtf8(
               isolate,
               listener_impl.seen.empty() ? "" : listener_impl.seen[0].c_str())
               .ToLocalChecked());
  out->Set(isolate, "notificationCount",
           Number::New(isolate,
                       static_cast<double>(listener_impl.seen.size())));
  info.GetReturnValue().Set(out);
}

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

}  // namespace

void InstallEchoBindings(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> wcb = GetOrCreateNamespace(isolate, global, "wcb");
  InstallFn(isolate, context, wcb, "echo", JsEcho);
  InstallFn(isolate, context, wcb, "echoWithListener", JsEchoWithListener);
}

}  // namespace wcb_bindings
