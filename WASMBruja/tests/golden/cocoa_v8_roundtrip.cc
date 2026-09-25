#include "bruja_cocoa/cocoa_impl.h"

#include <cstdio>
#include <string>

#include "v8.h"

namespace {

bool Eval(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, src);
  if (script.IsEmpty()) return false;
  return !script.ToLocalChecked()->Run(context).IsEmpty();
}

bool EvalBool(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result =
      v8::Script::Compile(context, src).ToLocalChecked()->Run(context).ToLocalChecked();
  return result->IsBoolean() && result.As<v8::Boolean>()->Value();
}

std::string EvalString(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Value> result =
      v8::Script::Compile(context, src).ToLocalChecked()->Run(context).ToLocalChecked();
  v8::String::Utf8Value utf8(isolate, result);
  return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  bruja_cocoa::NSStringExtrasImpl strings;
  bruja_cocoa::NSURLExtrasImpl urls;
  bool ok = false;
  std::string upper;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    context->Global()->Set(
        isolate, "ns",
        bruja_generated::CreateNSStringExtrasBinding(isolate, context, &strings));
    context->Global()->Set(
        isolate, "nsurl",
        bruja_generated::CreateNSURLExtrasBinding(isolate, context, &urls));

    ok = EvalBool(context, "ns.hasPrefix('WebKit', 'Web')") &&
         EvalBool(context, "ns.hasSuffix('WebKit', 'Kit')") &&
         EvalString(context, "ns.stringByAppendingString('Web', 'Kit')") ==
             "WebKit";
    upper = EvalString(context, "ns.convertToASCIIUppercase('webkit')");
    ok = ok && upper == "WEBKIT";
    ok = ok && EvalBool(context, "nsurl.isValid('https://example.test/')");
    ok = ok &&
         EvalString(context, "nsurl.host('https://example.test/x')") ==
             "example.test";

    bruja_generated::TeardownNSStringExtrasV8Binding(isolate);
    bruja_generated::TeardownNSURLExtrasV8Binding(isolate);
    (void)Eval;
  }
  isolate->Dispose();
  if (!ok) {
    std::fprintf(stderr, "FAIL cocoa_v8_roundtrip\n");
    return 1;
  }
  std::printf("cocoa_v8_roundtrip: OK\n");
  return 0;
}
