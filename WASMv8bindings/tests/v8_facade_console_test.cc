// Golden test for the V8 embedder-API facade: ties together everything
// built in this repo -- cppgc for the C++ object's lifetime, the
// CppHeapPointerTable for a safe JS<->C++ handle, and the V8-shaped
// Isolate/Context/ObjectTemplate/FunctionTemplate/Script surface, with
// quickjs-ng actually executing the JS source. Proves the whole chain end
// to end: JS source calls console.log(...), which unwraps a real
// cppgc-managed C++ Console object through Object::Unwrap and records the
// message on the C++ side -- exactly the shape a future brujac V8 backend
// would generate for a real Web IDL interface.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/visitor.h"
#include "v8.h"

namespace {

constexpr v8::CppHeapPointerTag kConsoleTag = static_cast<v8::CppHeapPointerTag>(1);

class Console final : public cppgc::GarbageCollected<Console> {
 public:
  std::vector<std::string> logged;
  void Trace(cppgc::Visitor*) const {}
  void Log(const std::string& message) { logged.push_back(message); }
};

void ConsoleLogCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  Console* console = v8::Object::Unwrap<Console>(info.This(), kConsoleTag);
  assert(console != nullptr);
  v8::String::Utf8Value message(isolate, info[0]);
  console->Log(*message);
  info.GetReturnValue().SetUndefined();
}

}  // namespace

int main() {
  auto cppgc_heap = cppgc::Heap::Create(nullptr);
  Console* console =
      cppgc::MakeGarbageCollected<Console>(cppgc_heap->GetAllocationHandle());

  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::ObjectTemplate> console_template =
        v8::ObjectTemplate::New(isolate);
    console_template->SetInternalFieldCount(1);
    console_template->Set("log",
                          v8::FunctionTemplate::New(isolate, ConsoleLogCallback));

    v8::Local<v8::Object> console_obj =
        console_template->NewInstance(context).ToLocalChecked();
    v8::Object::Wrap(isolate, console_obj, console, kConsoleTag);

    context->Global()->Set(isolate, "console", console_obj);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(isolate,
                                "console.log('hello from JS'); "
                                "console.log('second call'); "
                                "1 + 41")
            .ToLocalChecked();
    v8::Local<v8::Script> script =
        v8::Script::Compile(context, source).ToLocalChecked();
    v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

    assert(result->IsNumber());
    assert(result.As<v8::Number>()->Value() == 42.0);
  }

  // The C++ side really recorded both calls -- this didn't just wire up
  // dead plumbing, JS execution genuinely reached the cppgc-managed object.
  assert(console->logged.size() == 2);
  assert(console->logged[0] == "hello from JS");
  assert(console->logged[1] == "second call");

  isolate->Dispose();
  cppgc_heap->ForceGarbageCollectionSlow("test", "shutdown");
  cppgc_heap.reset();

  std::printf("v8_facade_console_test: OK\n");
  return 0;
}
