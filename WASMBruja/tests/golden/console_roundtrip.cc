// Proves brujac's generated Console binding actually works against a real
// quickjs-ng context -- not just that the generated header looks
// plausible. Mirrors the role of
// WASMVoodooCompile/tests/golden/echo_roundtrip.cc.
#include "console_gen.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

class ConsoleImpl : public bruja_generated::Console {
 public:
  void Log(const std::string& message) override { logs.push_back(message); }
  void Warn(const std::string& message) override { warns.push_back(message); }

  std::vector<std::string> logs;
  std::vector<std::string> warns;
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

}  // namespace

int main() {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  ConsoleImpl impl;
  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "console",
                     bruja_generated::CreateConsoleBinding(ctx, &impl));
  JS_FreeValue(ctx, global);

  bool ok = Eval(ctx, "console.log('hi'); console.warn('bye');");

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

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
    std::fprintf(stderr,
                 "FAIL: expected warns == [\"bye\"], got %zu entries\n",
                 impl.warns.size());
    return 1;
  }

  std::printf("console_roundtrip: OK\n");
  return 0;
}
