#include "v8-object.h"

#include "src/base/logging.h"
#include "v8-context.h"
#include "v8-isolate.h"

namespace v8 {

Local<Object> Object::New(Isolate* isolate) {
  JSContext* ctx = CurrentContext(isolate);
  CHECK(ctx);
  return Local<Object>::Adopt(ctx, JS_NewObject(ctx));
}

bool Object::Set(Isolate*, const char* name, Local<Value> value) {
  // JS_SetPropertyStr consumes (frees) its value argument on both success
  // and failure, so dup it -- `value` (a Local<Value>) still owns its own
  // reference and will free it normally when it goes out of scope.
  JSValue dup = JS_DupValue(ctx_, value.value_for_wasmv8_internal());
  return JS_SetPropertyStr(ctx_, val_, name, dup) >= 0;
}

MaybeLocal<Value> Object::Get(Isolate*, const char* name) const {
  JSValue result = JS_GetPropertyStr(ctx_, val_, name);
  if (JS_IsException(result)) {
    JS_FreeValue(ctx_, JS_GetException(ctx_));
    return MaybeLocal<Value>();
  }
  return Local<Value>::Adopt(ctx_, result);
}

void Object::WrapRaw(Isolate* isolate, Local<Object> object, void* cpp_object,
                     CppHeapPointerTag tag) {
  const JSValue val = object.value_for_wasmv8_internal();
  CHECK_EQ(JS_GetClassID(val), isolate->wrapper_class_id_for_wasmv8_internal());
  const CppHeapPointerHandle handle =
      isolate->cpp_heap_pointer_table_for_wasmv8_internal()
          .AllocateAndInitializeEntry(cpp_object, tag);
  const int ok =
      JS_SetOpaque(val, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
  CHECK_EQ(ok, 0);
}

void* Object::UnwrapRaw(Local<Object> object, CppHeapPointerTagRange tag_range) {
  JSContext* ctx = object.context_for_wasmv8_internal();
  if (!ctx) return nullptr;
  auto* isolate = static_cast<Isolate*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
  if (!isolate) return nullptr;
  const JSValue val = object.value_for_wasmv8_internal();
  if (JS_GetClassID(val) != isolate->wrapper_class_id_for_wasmv8_internal()) {
    return nullptr;
  }
  void* opaque = JS_GetOpaque(val, isolate->wrapper_class_id_for_wasmv8_internal());
  const auto handle =
      static_cast<CppHeapPointerHandle>(reinterpret_cast<uintptr_t>(opaque));
  return isolate->cpp_heap_pointer_table_for_wasmv8_internal().Get(handle,
                                                                   tag_range);
}

}  // namespace v8
