// Regression test for a real bug found (and fixed) while building brujac's
// V8 backend: the first version of cpp_generator_v8.cc built one *process-
// wide* v8::ObjectTemplate per interface, shared across every isolate.
// WASMv8bindings/src/v8/template.cc's ObjectTemplate::NewInstance uses the
// template's own captured isolate_'s wrapper_class_id_ -- a JSClassID
// allocated and registered on *that* isolate's own JSRuntime (see
// WASMv8bindings/src/v8/isolate.cc's constructor) -- combined with whatever
// JSContext the caller's Local<Context> happens to carry. A second,
// independent v8::Isolate (WASMv16 runs one per tab -- see WASMv16/include/
// wasmv16/engine.h) calling CreateConsoleBinding would have silently reused
// the first isolate's class id against the second isolate's own JSRuntime,
// where that id was never JS_NewClass'd -- the same "unregistered class"
// corruption cpp_generator.cc's own quickjs backend already documents (and
// avoids, via per-JSRuntime JS_IsRegisteredClass checks) for its analogous
// hazard. Fixed by keying the ObjectTemplate per v8::Isolate* instead (see
// EmitTagAndTemplateGlobals in cpp_generator_v8.cc). This test proves the
// fix: two real, independent isolates, each with its own Console binding,
// each actually called and each observed independently -- not just "didn't
// crash," a real behavioral check that neither isolate's binding leaked
// into or corrupted the other's.
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

void Eval(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Script::Compile(context, src).ToLocalChecked()->Run(context).ToLocalChecked();
}

}  // namespace

int main() {
  v8::Isolate* isolate_a = v8::Isolate::New();
  v8::Isolate* isolate_b = v8::Isolate::New();
  ConsoleImpl impl_a;
  ConsoleImpl impl_b;

  {
    v8::Isolate::Scope isolate_scope(isolate_a);
    v8::HandleScope handle_scope(isolate_a);
    v8::Local<v8::Context> context = v8::Context::New(isolate_a);
    v8::Context::Scope context_scope(context);
    context->Global()->Set(isolate_a, "console",
                           bruja_generated::CreateConsoleBinding(isolate_a, context, &impl_a));
    Eval(context, "console.log('from-a');");
  }
  {
    v8::Isolate::Scope isolate_scope(isolate_b);
    v8::HandleScope handle_scope(isolate_b);
    v8::Local<v8::Context> context = v8::Context::New(isolate_b);
    v8::Context::Scope context_scope(context);
    context->Global()->Set(isolate_b, "console",
                           bruja_generated::CreateConsoleBinding(isolate_b, context, &impl_b));
    Eval(context, "console.warn('from-b');");
  }
  // Back to isolate A -- a real, second, interleaved use, not just
  // construct-once-and-discard. This is exactly the shape that would have
  // hit the bug: isolate A's ObjectTemplate/class id being reused (wrongly)
  // for an object built against isolate B's JSContext, or vice versa.
  {
    v8::Isolate::Scope isolate_scope(isolate_a);
    v8::HandleScope handle_scope(isolate_a);
    v8::Local<v8::Context> context = v8::Context::New(isolate_a);
    v8::Context::Scope context_scope(context);
    context->Global()->Set(isolate_a, "console",
                           bruja_generated::CreateConsoleBinding(isolate_a, context, &impl_a));
    Eval(context, "console.log('from-a-again');");

    bruja_generated::TeardownConsoleV8Binding(isolate_a);
  }
  {
    v8::Isolate::Scope isolate_scope(isolate_b);
    v8::HandleScope handle_scope(isolate_b);
    bruja_generated::TeardownConsoleV8Binding(isolate_b);
  }

  isolate_a->Dispose();
  isolate_b->Dispose();

  int failures = 0;
  if (impl_a.logs.size() != 2 || impl_a.logs[0] != "from-a" ||
      impl_a.logs[1] != "from-a-again") {
    std::fprintf(stderr, "FAIL: impl_a.logs wrong (size=%zu)\n", impl_a.logs.size());
    ++failures;
  }
  if (!impl_a.warns.empty()) {
    std::fprintf(stderr, "FAIL: impl_a.warns should be empty, got %zu\n", impl_a.warns.size());
    ++failures;
  }
  if (impl_b.warns.size() != 1 || impl_b.warns[0] != "from-b") {
    std::fprintf(stderr, "FAIL: impl_b.warns wrong (size=%zu)\n", impl_b.warns.size());
    ++failures;
  }
  if (!impl_b.logs.empty()) {
    std::fprintf(stderr, "FAIL: impl_b.logs should be empty, got %zu\n", impl_b.logs.size());
    ++failures;
  }

  if (failures > 0) {
    std::fprintf(stderr, "console_v8_multi_isolate_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("console_v8_multi_isolate_roundtrip: OK\n");
  return 0;
}
