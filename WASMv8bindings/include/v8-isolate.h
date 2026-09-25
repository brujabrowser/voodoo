// Simplified from V8's include/v8-isolate.h. Real v8::Isolate owns V8's
// own JS heap, compilation caches, microtask queue, etc. Here it owns one
// quickjs JSRuntime (the actual JS engine -- see the top-level README's
// "V8 embedder-API facade" section) plus this port's own
// CppHeapPointerTable (matching real V8, which also keeps one
// CppHeapPointerTable per isolate) and the one quickjs class id used for
// every JS-wrapped cppgc object (see v8-object.h's Wrap()/Unwrap()).
//
// Not ported: microtasks, ArrayBuffer::Allocator/CreateParams beyond a
// placeholder, multiple contexts per isolate sharing compiled-script
// caches, snapshots.
#ifndef WASMV8_INCLUDE_V8_ISOLATE_H_
#define WASMV8_INCLUDE_V8_ISOLATE_H_

#include <vector>

#include "quickjs.h"
#include "src/sandbox/cppheap-pointer-table.h"

namespace v8 {

class Isolate {
 public:
  struct CreateParams {};

  static Isolate* New(const CreateParams& params = CreateParams());
  void Dispose();

  static Isolate* GetCurrent();

  // Runs quickjs pending jobs (promise reactions).
  void PerformMicrotaskCheckpoint();

  class Scope {
   public:
    explicit Scope(Isolate* isolate);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

   private:
    Isolate* previous_;
  };

  JSRuntime* runtime_for_wasmv8_internal() const { return rt_; }
  JSClassID wrapper_class_id_for_wasmv8_internal() const {
    return wrapper_class_id_;
  }
  cppgc::internal::CppHeapPointerTable& cpp_heap_pointer_table_for_wasmv8_internal() {
    return cpp_heap_pointer_table_;
  }

  // Contexts are owned by the Isolate that created them (see Context::New
  // in v8-context.h) and freed on Dispose() -- real V8 similarly keeps a
  // context alive at least as long as its creating isolate.
  void TrackContext_for_wasmv8_internal(JSContext* ctx) {
    contexts_.push_back(ctx);
  }

 private:
  Isolate();
  ~Isolate();

  JSRuntime* rt_;
  JSClassID wrapper_class_id_ = 0;
  cppgc::internal::CppHeapPointerTable cpp_heap_pointer_table_;
  std::vector<JSContext*> contexts_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_ISOLATE_H_
