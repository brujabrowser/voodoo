#include <cassert>
#include <cstdio>
#include <cstring>

#include "v8.h"

namespace {

void Run(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script = v8::Script::Compile(context, src).ToLocalChecked();
  script->Run(context).ToLocalChecked();
}

double RunNumber(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script = v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsNumber());
  return result.As<v8::Number>()->Value();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Promise::Resolver> resolver =
        v8::Promise::Resolver::New(context).ToLocalChecked();
    v8::Local<v8::Promise> promise = resolver->GetPromise();
    assert(promise->IsPromise());
    assert(promise->State() == v8::Promise::kPending);
    context->Global()->Set(isolate, "p", promise);
    Run(context, "p.then(function(v) { globalThis.got = v; })");
    assert(resolver->Resolve(context, v8::Number::New(isolate, 7)));
    assert(RunNumber(context, "globalThis.got === undefined ? 1 : 0") == 1);
    isolate->PerformMicrotaskCheckpoint();
    assert(promise->State() == v8::Promise::kFulfilled);
    assert(RunNumber(context, "got") == 7);

    const uint8_t bytes[] = {1, 2, 3, 4};
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, bytes, 4);
    assert(buffer->IsArrayBuffer());
    assert(buffer->ByteLength() == 4);
    assert(std::memcmp(buffer->Data(), bytes, 4) == 0);
    context->Global()->Set(isolate, "buf", buffer);
    assert(RunNumber(context, "buf.byteLength") == 4);

    std::printf("v8_facade_promise_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
