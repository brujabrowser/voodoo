// Proves brujac's v2 attribute codegen (readonly getter-only, and a
// read-write getter+setter pair) against a real quickjs-ng context. Mirrors
// console_roundtrip.cc's role for v1's methods.
#include "navigator_gen.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

class NavigatorImpl : public bruja_generated::Navigator {
 public:
  std::string UserAgent() override { return "WASMv16/1.0"; }

  bool OnLine() override { return on_line_; }
  void SetOnLine(bool value) override { on_line_ = value; }

 private:
  bool on_line_ = true;
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

  NavigatorImpl impl;
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "navigator",
                     bruja_generated::CreateNavigatorBinding(ctx, &impl));
  JS_FreeValue(ctx, global);

  int failures = 0;

  std::string ua = EvalString(ctx, "navigator.userAgent");
  if (ua != "WASMv16/1.0") {
    std::fprintf(stderr, "FAIL: userAgent getter returned '%s'\n",
                 ua.c_str());
    ++failures;
  }

  std::string online_before = EvalString(ctx, "String(navigator.onLine)");
  if (online_before != "true") {
    std::fprintf(stderr, "FAIL: onLine getter returned '%s', want 'true'\n",
                 online_before.c_str());
    ++failures;
  }

  if (!Eval(ctx, "navigator.onLine = false;")) {
    std::fprintf(stderr, "FAIL: setting onLine threw\n");
    ++failures;
  } else if (impl.OnLine() != false) {
    std::fprintf(stderr, "FAIL: setter didn't reach the C++ impl\n");
    ++failures;
  }

  // Assigning to a readonly attribute must throw (Eval returns false).
  if (Eval(ctx, "navigator.userAgent = 'nope';")) {
    std::fprintf(stderr,
                 "FAIL: assigning to readonly userAgent did not throw\n");
    ++failures;
  }

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (failures > 0) {
    std::fprintf(stderr, "navigator_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("navigator_roundtrip: OK\n");
  return 0;
}
