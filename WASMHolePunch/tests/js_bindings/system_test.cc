#include <cassert>
#include <cstdio>

#include "v8.h"
#include "whp/c/types.h"
#include "whp/js_bindings/system_bindings.h"

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

    whp_js_bindings::InstallSystemBindings(isolate, context);

    assert(RunNumber(context, "whp.init()") == WHP_RESULT_OK);
    assert(RunNumber(context, "whp.RESULT_OK") == WHP_RESULT_OK);

    Run(context, "globalThis.pipe = whp.createMessagePipe()");
    assert(RunNumber(context, "pipe.result") == WHP_RESULT_OK);
    assert(RunNumber(context, "whp.writeMessage(pipe.handle0, 'hello')") ==
           WHP_RESULT_OK);
    Run(context, "globalThis.msg = whp.readMessage(pipe.handle1)");
    assert(RunNumber(context, "msg.result") == WHP_RESULT_OK);
    assert(RunBool(context, "msg.payload === 'hello'"));
    assert(RunNumber(context, "whp.close(pipe.handle0)") == WHP_RESULT_OK);
    assert(RunNumber(context, "whp.close(pipe.handle1)") == WHP_RESULT_OK);

    Run(context, "globalThis.dp = whp.createDataPipe()");
    assert(RunNumber(context, "dp.result") == WHP_RESULT_OK);
    assert(RunNumber(context, "whp.writeData(dp.producer, 'abcd')") ==
           WHP_RESULT_OK);
    Run(context, "globalThis.dmsg = whp.readData(dp.consumer, 8)");
    assert(RunNumber(context, "dmsg.result") == WHP_RESULT_OK);
    assert(RunBool(context, "dmsg.payload === 'abcd'"));
    assert(RunNumber(context, "whp.close(dp.producer)") == WHP_RESULT_OK);
    assert(RunNumber(context, "whp.close(dp.consumer)") == WHP_RESULT_OK);

    Run(context, "whp.shutdown()");
    std::printf("whp_bindings_system_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
