// Same proof as console_roundtrip.cc, against brujac's V8 backend
// (--backend=v8, cpp_generator_v8.cc) instead of the quickjs one: a real
// round trip through the WASMv8bindings facade, not just a header that
// looks plausible.
#include "console_v8_gen.h"

#include <cstdio>
#include <string>
#include <vector>

#include "v8.h"

namespace {

class ConsoleImpl : public bruja_generated::Console {
 public:
  void Log(const std::string& message) override { logs.push_back(message); }
  void Warn(const std::string& message) override { warns.push_back(message); }

  std::vector<std::string> logs;
  std::vector<std::string> warns;
};

bool Eval(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, src);
  if (script.IsEmpty()) return false;
  v8::MaybeLocal<v8::Value> result = script.ToLocalChecked()->Run(context);
  return !result.IsEmpty();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  bool ok = true;
  ConsoleImpl impl;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    context->Global()->Set(
        isolate, "console",
        bruja_generated::CreateConsoleBinding(isolate, context, &impl));

    ok = Eval(context, "console.log('hi'); console.warn('bye');");

    bruja_generated::TeardownConsoleV8Binding(isolate);
  }
  isolate->Dispose();

  if (!ok) {
    std::fprintf(stderr, "FAIL: eval failed\n");
    return 1;
  }
  if (impl.logs.size() != 1 || impl.logs[0] != "hi") {
    std::fprintf(stderr, "FAIL: expected logs == [\"hi\"], got %zu entries\n",
                 impl.logs.size());
    return 1;
  }
  if (impl.warns.size() != 1 || impl.warns[0] != "bye") {
    std::fprintf(stderr, "FAIL: expected warns == [\"bye\"], got %zu entries\n",
                 impl.warns.size());
    return 1;
  }

  std::printf("console_v8_roundtrip: OK\n");
  return 0;
}
