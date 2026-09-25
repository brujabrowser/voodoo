// Same proof as navigator_roundtrip.cc, against brujac's V8 backend -- a
// real getter/setter attribute round trip through the WASMv8bindings
// facade's ObjectTemplate::SetAccessor.
#include "navigator_v8_gen.h"

#include <cstdio>
#include <string>

#include "v8.h"

namespace {

class NavigatorImpl : public bruja_generated::Navigator {
 public:
  std::string UserAgent() override { return "BrujaBrowser/1.0"; }
  bool OnLine() override { return on_line_; }
  void SetOnLine(bool value) override { on_line_ = value; }

  bool on_line_ = true;
};

double RunNumber(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result = v8::Script::Compile(context, src).ToLocalChecked()
                                     ->Run(context)
                                     .ToLocalChecked();
  return result.As<v8::Number>()->Value();
}

bool RunBool(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result = v8::Script::Compile(context, src).ToLocalChecked()
                                     ->Run(context)
                                     .ToLocalChecked();
  return result.As<v8::Boolean>()->Value();
}

std::string RunString(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result = v8::Script::Compile(context, src).ToLocalChecked()
                                     ->Run(context)
                                     .ToLocalChecked();
  v8::String::Utf8Value utf8(isolate, result);
  return std::string(*utf8);
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

    NavigatorImpl impl;
    context->Global()->Set(
        isolate, "navigator",
        bruja_generated::CreateNavigatorBinding(isolate, context, &impl));

    std::string ua = RunString(context, "navigator.userAgent");
    if (ua != "BrujaBrowser/1.0") {
      std::fprintf(stderr, "FAIL: userAgent = %s\n", ua.c_str());
      ++failures;
    }

    if (!RunBool(context, "navigator.onLine")) {
      std::fprintf(stderr, "FAIL: onLine should start true\n");
      ++failures;
    }

    Run(context, "navigator.onLine = false;");
    if (impl.on_line_ != false) {
      std::fprintf(stderr, "FAIL: setter didn't reach the real impl\n");
      ++failures;
    }
    if (RunBool(context, "navigator.onLine")) {
      std::fprintf(stderr, "FAIL: onLine should read back false\n");
      ++failures;
    }

    // readonly attribute: the facade has no throw support yet (see
    // cpp_generator_v8.cc's file comment), so assigning to userAgent is a
    // real, honest no-op -- not an error -- and the real value is
    // unaffected either way.
    Run(context, "navigator.userAgent = 'nope';");
    if (RunString(context, "navigator.userAgent") != "BrujaBrowser/1.0") {
      std::fprintf(stderr, "FAIL: readonly attribute should not have changed\n");
      ++failures;
    }
    (void)RunNumber;

    bruja_generated::TeardownNavigatorV8Binding(isolate);
  }
  isolate->Dispose();

  if (failures > 0) {
    std::fprintf(stderr, "navigator_v8_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("navigator_v8_roundtrip: OK\n");
  return 0;
}
