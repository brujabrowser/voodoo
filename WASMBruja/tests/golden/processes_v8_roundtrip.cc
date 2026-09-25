// Same proof as processes_roundtrip.cc, against brujac's V8 backend -- long/
// boolean method params and returns (not just DOMString) through the
// WASMv8bindings facade.
#include "processes_v8_gen.h"

#include <cstdio>
#include <string>

#include "v8.h"

namespace {

// Same in-memory stand-in as processes_roundtrip.cc -- proves the generated
// marshaling reaches a real C++ implementation with the right values, in
// both directions.
class ProcessesImpl : public bruja_generated::Processes {
 public:
  int32_t GetProcessIdForTab(int32_t tabId) override { return tabId * 10 + 1; }

  bool Terminate(int32_t processId) override {
    last_terminated = processId;
    return processId != 0;
  }

  std::string GetProcessInfo(const std::string& processIdsJson, bool includeMemory) override {
    return "{\"echo\":" + processIdsJson + ",\"mem\":" + (includeMemory ? "true" : "false") + "}";
  }

  int32_t last_terminated = -1;
};

std::string EvalString(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result = v8::Script::Compile(context, src).ToLocalChecked()
                                     ->Run(context)
                                     .ToLocalChecked();
  if (result->IsString()) {
    v8::String::Utf8Value utf8(isolate, result);
    return std::string(*utf8);
  }
  if (result->IsBoolean()) return result.As<v8::Boolean>()->Value() ? "true" : "false";
  if (result->IsNumber()) return std::to_string(result.As<v8::Number>()->Value());
  return "<other>";
}

void Run(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Script::Compile(context, src).ToLocalChecked()->Run(context).ToLocalChecked();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  int failures = 0;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    ProcessesImpl impl;
    context->Global()->Set(
        isolate, "processes",
        bruja_generated::CreateProcessesBinding(isolate, context, &impl));

    std::string id = EvalString(context, "processes.getProcessIdForTab(3)");
    if (id != "31.000000") {
      std::fprintf(stderr, "FAIL: getProcessIdForTab(3) = %s, want 31\n", id.c_str());
      ++failures;
    }

    std::string refused = EvalString(context, "processes.terminate(0)");
    if (refused != "false") {
      std::fprintf(stderr, "FAIL: terminate(0) = %s, want false\n", refused.c_str());
      ++failures;
    }

    std::string ok = EvalString(context, "processes.terminate(31)");
    if (ok != "true" || impl.last_terminated != 31) {
      std::fprintf(stderr, "FAIL: terminate(31) = %s, last_terminated=%d\n", ok.c_str(),
                   impl.last_terminated);
      ++failures;
    }

    std::string info = EvalString(context, "processes.getProcessInfo('[1,2]', true)");
    if (info != "{\"echo\":[1,2],\"mem\":true}") {
      std::fprintf(stderr, "FAIL: getProcessInfo(...) = %s\n", info.c_str());
      ++failures;
    }

    // Wrong-arity call: the facade has no throw support yet (see
    // cpp_generator_v8.cc's file comment), so this is a real, honest
    // `undefined` return rather than a thrown TypeError -- not a crash,
    // which is what actually matters here.
    Run(context, "processes.getProcessIdForTab();");

    bruja_generated::TeardownProcessesV8Binding(isolate);
  }
  isolate->Dispose();

  if (failures > 0) {
    std::fprintf(stderr, "processes_v8_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("processes_v8_roundtrip: OK\n");
  return 0;
}
