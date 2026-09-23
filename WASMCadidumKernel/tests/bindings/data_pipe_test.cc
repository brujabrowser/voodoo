// Golden test for the mojo.* data-pipe bindings -- mirrors
// tests/system/data_pipe_roundtrip.cc's DataPipeWriteRead case, but
// drives it from JS on the WASMv8bindings facade.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/types.h"
#include "v8.h"
#include "wck/bindings/data_pipe_bindings.h"

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

    wck_bindings::InstallDataPipeBindings(isolate, context);

    assert(RunNumber(context, "mojo.RESULT_OK") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.HANDLE_INVALID") == MOJO_HANDLE_INVALID);

    Run(context, "globalThis.pipe = mojo.createDataPipe()");
    assert(RunNumber(context, "pipe.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "pipe.producer") != MOJO_HANDLE_INVALID);
    assert(RunNumber(context, "pipe.consumer") != MOJO_HANDLE_INVALID);

    assert(RunNumber(context, "mojo.writeData(pipe.producer, 'abcd')") ==
           MOJO_RESULT_OK);
    Run(context, "globalThis.msg = mojo.readData(pipe.consumer, 4)");
    assert(RunNumber(context, "msg.result") == MOJO_RESULT_OK);
    assert(RunBool(context, "msg.payload === 'abcd'"));

    assert(RunNumber(context, "mojo.close(pipe.producer)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(pipe.consumer)") == MOJO_RESULT_OK);

    std::printf("wck_bindings_data_pipe_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
