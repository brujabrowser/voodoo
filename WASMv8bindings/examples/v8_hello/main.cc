// Minimal usage of the V8 embedder-API facade: create an isolate and
// context, run a JS source string, print the result. No cppgc wrapper
// object involved -- see tests/v8_facade_console_test.cc for that.
//
// argv[1], if given, replaces the default source string -- this is the
// hook BrowserEmu's `-runtime=v8 -eval "<script>"` execs into. The source
// echoes to stderr for humans; stdout carries only the result (or
// "ERROR: <message>" on a thrown/compile exception, never a crash) so
// scripts (BrowserEmu's parity harness) can capture it without parsing
// it out of a label.
#include <cstdio>

#include "v8.h"

int main(int argc, char** argv) {
  const char* source_str = argc > 1 ? argv[1] : "1 + 2 + 39";

  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    std::fprintf(stderr, "source: %s\n", source_str);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(isolate, source_str).ToLocalChecked();
    v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, source);
    v8::MaybeLocal<v8::Value> result;
    if (!script.IsEmpty()) {
      result = script.ToLocalChecked()->Run(context);
    }

    if (result.IsEmpty()) {
      v8::String::Utf8Value err(isolate, try_catch.Exception());
      std::printf("ERROR: %s\n", *err ? *err : "(unknown)");
    } else {
      v8::String::Utf8Value utf8(isolate, result.ToLocalChecked());
      std::printf("%s\n", *utf8);
    }
  }
  isolate->Dispose();
  return 0;
}
