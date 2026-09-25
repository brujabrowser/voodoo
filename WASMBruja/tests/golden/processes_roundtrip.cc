// Proves brujac's v3 codegen (long/boolean method params and returns, not
// just DOMString) against a real quickjs-ng context -- ast.h's TypeKind
// already had kLong/kUnsignedLong/kBoolean marshaling in cpp_generator.cc,
// but no golden test had exercised it end to end until this one. Mirrors
// console_roundtrip.cc/navigator_roundtrip.cc's role for v1/v2.
#include "processes_gen.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// In-memory stand-in. Enough to prove the generated marshaling reaches a
// real C++ implementation with the right values, in both directions.
class ProcessesImpl : public bruja_generated::Processes {
 public:
  int32_t GetProcessIdForTab(int32_t tabId) override { return tabId * 10 + 1; }

  bool Terminate(int32_t processId) override {
    last_terminated = processId;
    return processId != 0;  // process 0 ("browser") refused, real-shaped
  }

  std::string GetProcessInfo(const std::string& processIdsJson,
                              bool includeMemory) override {
    return "{\"echo\":" + processIdsJson +
           ",\"mem\":" + (includeMemory ? "true" : "false") + "}";
  }

  int32_t last_terminated = -1;
};

bool Eval(JSContext* ctx, const char* source) {
  JSValue result = JS_Eval(ctx, source, strlen(source), "<test>",
                            JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
    JSValue exc = JS_GetException(ctx);
    const char* msg = JS_ToCString(ctx, exc);
    std::fprintf(stderr, "eval error: %s\n", msg ? msg : "(unknown)");
    if (msg) JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, exc);
    JS_FreeValue(ctx, result);
    return false;
  }
  JS_FreeValue(ctx, result);
  return true;
}

std::string EvalString(JSContext* ctx, const char* source) {
  JSValue result =
      JS_Eval(ctx, source, strlen(source), "<test>", JS_EVAL_TYPE_GLOBAL);
  const char* cstr = JS_ToCString(ctx, result);
  std::string out = cstr ? cstr : "";
  if (cstr) JS_FreeCString(ctx, cstr);
  JS_FreeValue(ctx, result);
  return out;
}

}  // namespace

int main() {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  ProcessesImpl impl;
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "processes",
                     bruja_generated::CreateProcessesBinding(ctx, &impl));
  JS_FreeValue(ctx, global);

  int failures = 0;

  std::string id = EvalString(ctx, "String(processes.getProcessIdForTab(3))");
  if (id != "31") {
    std::fprintf(stderr, "FAIL: getProcessIdForTab(3) = %s, want 31\n", id.c_str());
    ++failures;
  }

  std::string refused = EvalString(ctx, "String(processes.terminate(0))");
  if (refused != "false") {
    std::fprintf(stderr, "FAIL: terminate(0) = %s, want false\n", refused.c_str());
    ++failures;
  }

  std::string ok = EvalString(ctx, "String(processes.terminate(31))");
  if (ok != "true" || impl.last_terminated != 31) {
    std::fprintf(stderr, "FAIL: terminate(31) = %s, last_terminated=%d\n", ok.c_str(),
                 impl.last_terminated);
    ++failures;
  }

  std::string info =
      EvalString(ctx, "processes.getProcessInfo('[1,2]', true)");
  if (info != "{\"echo\":[1,2],\"mem\":true}") {
    std::fprintf(stderr, "FAIL: getProcessInfo(...) = %s\n", info.c_str());
    ++failures;
  }

  // Wrong-arity/type calls should throw (JS_EXCEPTION), not crash.
  if (Eval(ctx, "processes.getProcessIdForTab();")) {
    std::fprintf(stderr, "FAIL: missing-arg call did not throw\n");
    ++failures;
  }

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (failures > 0) {
    std::fprintf(stderr, "processes_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("processes_roundtrip: OK\n");
  return 0;
}
