// Golden test for the wst.* message-pipe bindings -- mirrors
// tests/core/message_pipe_roundtrip.cc's create / write / read / close
// path, but drives it from JS on the WASMv8bindings facade.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/types.h"
#include "v8.h"
#include "wst/bindings/message_pipe_bindings.h"

namespace {

double RunNumber(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsNumber());
  return result.As<v8::Number>()->Value();
}

bool RunBool(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsBoolean());
  return result.As<v8::Boolean>()->Value();
}

void Run(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  script->Run(context).ToLocalChecked();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    wst_bindings::InstallMessagePipeBindings(isolate, context);

    assert(RunNumber(context, "wst.RESULT_OK") == MOJO_RESULT_OK);
    assert(RunNumber(context, "wst.HANDLE_INVALID") == MOJO_HANDLE_INVALID);

    Run(context, "globalThis.pipe = wst.createMessagePipe()");
    assert(RunNumber(context, "pipe.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "pipe.handle0") != MOJO_HANDLE_INVALID);
    assert(RunNumber(context, "pipe.handle1") != MOJO_HANDLE_INVALID);

    assert(RunNumber(context,
                     "wst.writeMessage(pipe.handle0, 'hello thunker')") ==
           MOJO_RESULT_OK);
    Run(context, "globalThis.msg = wst.readMessage(pipe.handle1)");
    assert(RunNumber(context, "msg.result") == MOJO_RESULT_OK);
    assert(RunBool(context, "msg.payload === 'hello thunker'"));

    assert(RunNumber(context, "wst.close(pipe.handle0)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "wst.close(pipe.handle1)") == MOJO_RESULT_OK);

    std::printf("wst_bindings_message_pipe_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
