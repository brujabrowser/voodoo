// Golden test for TryCatch: proves a thrown JS exception is actually
// captured (the real value, not just "something failed") while a TryCatch
// is active, and that without one active the same throwing script still
// just fails cleanly (MaybeLocal empty) exactly as it already did before
// TryCatch existed -- adding capture must not change the no-TryCatch
// behavior.
#include <cassert>
#include <cstdio>
#include <string>

#include "v8.h"

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    // A thrown Error is captured, and its message is the real thrown text.
    {
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(isolate, "throw new Error('boom');").ToLocalChecked();
      v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
      v8::MaybeLocal<v8::Value> result = script->Run(context);
      assert(result.IsEmpty());
      assert(try_catch.HasCaught());
      v8::String::Utf8Value message(isolate, try_catch.Exception());
      assert(std::string(*message).find("boom") != std::string::npos);
    }

    // A thrown non-Error value (a plain string) is captured verbatim too.
    {
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(isolate, "throw 'plain-string-throw';").ToLocalChecked();
      v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
      assert(script->Run(context).IsEmpty());
      assert(try_catch.HasCaught());
      v8::String::Utf8Value message(isolate, try_catch.Exception());
      assert(std::string(*message) == "plain-string-throw");
    }

    // A nested TryCatch only sees its own scope's exception; the outer one
    // is untouched by it.
    {
      v8::TryCatch outer(isolate);
      {
        v8::TryCatch inner(isolate);
        v8::Local<v8::String> source =
            v8::String::NewFromUtf8(isolate, "throw 'inner';").ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
        assert(script->Run(context).IsEmpty());
        assert(inner.HasCaught());
      }
      assert(!outer.HasCaught());
    }

    // Without a TryCatch active, a throwing script still just fails
    // cleanly (unchanged pre-TryCatch behavior) -- nothing crashes trying
    // to report an exception nobody is watching for.
    {
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(isolate, "throw 'unwatched';").ToLocalChecked();
      v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
      assert(script->Run(context).IsEmpty());
    }

    // A successful (non-throwing) script inside a TryCatch reports no
    // exception.
    {
      v8::TryCatch try_catch(isolate);
      v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, "1 + 1").ToLocalChecked();
      v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
      assert(!script->Run(context).IsEmpty());
      assert(!try_catch.HasCaught());
    }
  }

  isolate->Dispose();
  std::printf("v8_facade_trycatch_test: OK\n");
  return 0;
}
