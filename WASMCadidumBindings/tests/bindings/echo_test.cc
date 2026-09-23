// Golden test for wcb.echo -- mirrors
// tests/bindings/remote_receiver_roundtrip.cc's request/response case,
// driven from JS on the WASMv8bindings facade.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/core.h"
#include "v8.h"
#include "wcb/bindings/echo_bindings.h"

namespace {

bool RunBool(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsBoolean());
  return result.As<v8::Boolean>()->Value();
}

}  // namespace

int main() {
  MojoInitialize(nullptr);

  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    wcb_bindings::InstallEchoBindings(isolate, context);

    assert(RunBool(context, "wcb.echo('hello via wcb') === 'hello via wcb'"));
    assert(RunBool(context, "wcb.echo('') === ''"));

    assert(RunBool(
        context,
        "globalThis.assoc = wcb.echoWithListener('assoc'); "
        "assoc.echo === 'assoc' && assoc.notification === 'pushed:assoc' && "
        "assoc.notificationCount === 1"));

    std::printf("wcb_bindings_echo_test: ok\n");
  }
  isolate->Dispose();

  MojoShutdown(nullptr);
  return 0;
}
