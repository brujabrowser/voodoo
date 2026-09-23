// Golden test for mojo.* shared-buffer bindings -- mirrors
// tests/system/buffer_roundtrip.cc from JS on the WASMv8bindings facade.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/types.h"
#include "v8.h"
#include "wck/bindings/buffer_bindings.h"

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

    wck_bindings::InstallBufferBindings(isolate, context);

    Run(context, "globalThis.buf = mojo.createSharedBuffer(64)");
    assert(RunNumber(context, "buf.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "buf.handle") != MOJO_HANDLE_INVALID);

    assert(RunNumber(context, "mojo.mapWrite(buf.handle, 0, 64, 'kernel')") ==
           MOJO_RESULT_OK);

    Run(context, "globalThis.dup = mojo.duplicateBuffer(buf.handle)");
    assert(RunNumber(context, "dup.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "dup.handle") != MOJO_HANDLE_INVALID);

    Run(context, "globalThis.read = mojo.mapRead(dup.handle, 0, 64)");
    assert(RunNumber(context, "read.result") == MOJO_RESULT_OK);
    assert(RunBool(context, "read.payload === 'kernel'"));

    assert(RunNumber(context, "mojo.close(buf.handle)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(dup.handle)") == MOJO_RESULT_OK);

    std::printf("wck_bindings_buffer_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
