#include "echo_messages_v8_gen.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "v8.h"

namespace {

class EchoImpl : public bruja_generated::Echo {
 public:
  void SetMode(uint8_t mode) override { mode_ = mode; }
  std::string Send(const std::string& text) override {
    last_ = text;
    return text;
  }
  bool Ping() override { return true; }

  uint8_t mode_ = 0;
  std::string last_;
};

bool EvalBool(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result =
      v8::Script::Compile(context, src).ToLocalChecked()->Run(context).ToLocalChecked();
  return result->IsBoolean() && result.As<v8::Boolean>()->Value();
}

bool Eval(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, src);
  if (script.IsEmpty()) return false;
  return !script.ToLocalChecked()->Run(context).IsEmpty();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  EchoImpl impl;
  bool ok = false;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    context->Global()->Set(
        isolate, "echo",
        bruja_generated::CreateEchoBinding(isolate, context, &impl));

    ok = Eval(context, "echo.setMode(3);") && EvalBool(context, "echo.ping()");
    bruja_generated::TeardownEchoV8Binding(isolate);
  }
  isolate->Dispose();
  if (!ok || impl.mode_ != 3) {
    std::fprintf(stderr, "FAIL messages_in_v8_roundtrip mode=%u\n", impl.mode_);
    return 1;
  }
  std::printf("messages_in_v8_roundtrip: OK\n");
  return 0;
}
