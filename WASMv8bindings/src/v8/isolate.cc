#include "v8-isolate.h"

#include "src/base/logging.h"

namespace v8 {

namespace {
thread_local Isolate* g_current_isolate = nullptr;

// Runs when quickjs's own GC collects a JS wrapper object (one created via
// ObjectTemplate::NewInstance with SetInternalFieldCount() >= 1): frees the
// corresponding CppHeapPointerTable entry so the handle can't outlive the
// JS side that referenced it. Note this does NOT finalize the wrapped
// cppgc object itself -- that object's own lifetime is managed by this
// port's cppgc Heap (see the top-level README), completely independently;
// a JS wrapper going away just means JS can no longer reach the C++
// object through *this* handle.
void WrapperFinalizer(JSRuntime* rt, JSValueConst val) {
  auto* isolate = static_cast<Isolate*>(JS_GetRuntimeOpaque(rt));
  if (!isolate) return;
  void* opaque = JS_GetOpaque(val, isolate->wrapper_class_id_for_wasmv8_internal());
  const auto handle =
      static_cast<CppHeapPointerHandle>(reinterpret_cast<uintptr_t>(opaque));
  isolate->cpp_heap_pointer_table_for_wasmv8_internal().FreeEntry(handle);
}
}  // namespace

Isolate::Isolate() : rt_(JS_NewRuntime()) {
  CHECK(rt_);
  JS_SetRuntimeOpaque(rt_, this);
  JS_NewClassID(rt_, &wrapper_class_id_);
  JSClassDef def = {};
  def.class_name = "WasmV8Wrapper";
  def.finalizer = WrapperFinalizer;
  JS_NewClass(rt_, wrapper_class_id_, &def);
}

Isolate::~Isolate() {
  for (JSContext* ctx : contexts_) {
    JS_FreeContext(ctx);
  }
  JS_FreeRuntime(rt_);
}

Isolate* Isolate::New(const CreateParams&) { return new Isolate(); }

void Isolate::Dispose() { delete this; }

Isolate* Isolate::GetCurrent() { return g_current_isolate; }

Isolate::Scope::Scope(Isolate* isolate) : previous_(g_current_isolate) {
  g_current_isolate = isolate;
}

Isolate::Scope::~Scope() { g_current_isolate = previous_; }

}  // namespace v8
