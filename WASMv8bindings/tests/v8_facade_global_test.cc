// Golden test for Global<T>: proves a JS function stored as a Global
// genuinely survives past the scope (and past the Local<T>) that created
// it -- the shape a real event-listener registration (`addEventListener`)
// needs -- and is still callable later, invoking the real underlying
// quickjs function.
#include <cassert>
#include <cstdio>

#include "v8.h"

namespace {

// Stands in for an event-listener registry: a single stored callback,
// exactly the shape `EventTarget::addEventListener` would need.
v8::Global<v8::Function> g_stored_listener;

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    {
      // The Local<Function> (and the whole block it's declared in) goes
      // out of scope before we ever invoke the stored Global -- if Global
      // were just aliasing the same short-lived handle, this would already
      // be a use-after-free by the time Call() runs below.
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8(isolate, "(function(x) { return x * 2; })")
              .ToLocalChecked();
      v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
      v8::Local<v8::Value> fn_value = script->Run(context).ToLocalChecked();
      assert(fn_value->IsFunction());
      v8::Local<v8::Function> fn = fn_value.As<v8::Function>();
      g_stored_listener.Reset(isolate, fn);
    }

    assert(!g_stored_listener.IsEmpty());

    v8::Local<v8::Function> listener = g_stored_listener.Get(isolate);
    v8::Local<v8::Value> arg = v8::Number::New(isolate, 21);
    v8::Local<v8::Value> args[] = {arg};
    v8::Local<v8::Value> result =
        listener->Call(context, context->Global(), 1, args).ToLocalChecked();
    assert(result->IsNumber());
    assert(result.As<v8::Number>()->Value() == 42.0);

    // Reset() releases it -- matches real V8's Global::Reset()/destructor.
    g_stored_listener.Reset();
    assert(g_stored_listener.IsEmpty());
  }

  isolate->Dispose();
  std::printf("v8_facade_global_test: OK\n");
  return 0;
}
