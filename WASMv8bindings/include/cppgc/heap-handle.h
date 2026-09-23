// Minimal stand-in for V8's include/cppgc/heap-handle.h -- an opaque marker
// type identifying a Heap instance in other APIs. Since this port supports
// only one live Heap (see heap.h), nothing besides identity is needed yet.
#ifndef WASMV8_INCLUDE_CPPGC_HEAP_HANDLE_H_
#define WASMV8_INCLUDE_CPPGC_HEAP_HANDLE_H_

#include "v8config.h"

namespace cppgc {

namespace internal {
class Heap;
}  // namespace internal

class V8_EXPORT HeapHandle {
 private:
  HeapHandle() = default;
  friend class internal::Heap;
};

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_HEAP_HANDLE_H_
