// Simplified from V8's include/v8-object.h. Real v8::Object has dozens of
// property-access overloads (interceptors, property attributes, own vs.
// inherited, array indices, ...); this facade covers named
// get/set/CreateDataProperty and real V8's newer Wrap()/Unwrap() pair for
// tying a JS object to a C++ one -- backed here by
// CppHeapPointerTable (src/sandbox/cppheap-pointer-table.h) exactly the
// way real V8 backs Wrap()/Unwrap() when V8_COMPRESS_POINTERS is on.
#ifndef WASMV8_INCLUDE_V8_OBJECT_H_
#define WASMV8_INCLUDE_V8_OBJECT_H_

#include "quickjs.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-sandbox.h"
#include "v8-value.h"

namespace v8 {

class Context;

class Object : public Value {
 public:
  Object() = default;
  Object(JSContext* ctx, JSValue val) : Value(ctx, val) {}

  // A plain, non-wrappable object (JS_CLASS_OBJECT) -- cannot be used with
  // Wrap()/Unwrap(), matching real V8's requirement that a wrappable object
  // come from a Template with an internal field. Use
  // ObjectTemplate::NewInstance() (v8-template.h) for a wrappable one.
  static Local<Object> New(Isolate* isolate);

  bool Set(Isolate* isolate, const char* name, Local<Value> value);
  MaybeLocal<Value> Get(Isolate* isolate, const char* name) const;

  // Associates `cpp_object` with this JS object through the isolate's
  // CppHeapPointerTable, tagged `tag`. `object` must have been created
  // wrappable (see ObjectTemplate::NewInstance) -- CHECK-fails otherwise.
  // The association is torn down automatically (the table entry freed)
  // when this JS object is garbage collected by quickjs.
  template <typename T>
  static void Wrap(Isolate* isolate, Local<Object> object, T* cpp_object,
                   CppHeapPointerTag tag) {
    WrapRaw(isolate, object, cpp_object, tag);
  }

  // Recovers the C++ object Wrap() associated with `object`, if `object` is
  // wrappable and its stored tag falls within `tag_range`. Returns nullptr
  // on any mismatch (not wrappable, never wrapped, or wrong type) -- a
  // type-confused caller gets a clean null, never the raw pointer; see
  // CppHeapPointerTable::Get's own file comment.
  template <typename T>
  static T* Unwrap(Local<Object> object, CppHeapPointerTagRange tag_range) {
    return static_cast<T*>(UnwrapRaw(object, tag_range));
  }

 private:
  static void WrapRaw(Isolate* isolate, Local<Object> object,
                      void* cpp_object, CppHeapPointerTag tag);
  static void* UnwrapRaw(Local<Object> object, CppHeapPointerTagRange tag_range);
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_OBJECT_H_
