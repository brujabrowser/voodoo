// Simplified from V8's include/cppgc/heap.h.
//
// This port supports exactly one Heap::StackSupport
// (kNoConservativeStackScan), one MarkingType (kAtomic) and one
// SweepingType (kAtomic) -- see the top-level README for why: no
// OS-portable conservative stack scan on wasm32-wasip1, and no concurrent
// marking/sweeping without threads. Heap::Create() CHECK-fails if asked for
// anything else rather than silently downgrading, so a port that later adds
// real incremental/concurrent support can loosen this without breaking
// existing callers. Custom spaces (real cppgc's HeapOptions::custom_spaces)
// are not yet ported.
//
// Also a v1 scope limitation not present in real cppgc: only one Heap
// instance may be alive at a time in this port (see persistent.h's file
// comment for why Persistent<T> relies on this). Heap::Create() CHECK-fails
// on a second concurrent instance.
#ifndef WASMV8_INCLUDE_CPPGC_HEAP_H_
#define WASMV8_INCLUDE_CPPGC_HEAP_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "cppgc/allocation.h"
#include "cppgc/common.h"
#include "cppgc/heap-handle.h"
#include "cppgc/platform.h"
#include "v8config.h"

namespace cppgc {

namespace internal {
class Heap;
}  // namespace internal

class V8_EXPORT Heap {
 public:
  using StackState = EmbedderStackState;

  enum class StackSupport : uint8_t {
    kSupportsConservativeStackScan,
    kNoConservativeStackScan,
  };
  enum class MarkingType : uint8_t { kAtomic, kIncremental, kIncrementalAndConcurrent };
  enum class SweepingType : uint8_t { kAtomic, kIncremental, kIncrementalAndConcurrent };

  struct HeapOptions {
    static HeapOptions Default() { return {}; }

    StackSupport stack_support = StackSupport::kNoConservativeStackScan;
    MarkingType marking_support = MarkingType::kAtomic;
    SweepingType sweeping_support = SweepingType::kAtomic;
  };

  static std::unique_ptr<Heap> Create(std::shared_ptr<Platform> platform,
                                      HeapOptions options = HeapOptions::Default());

  virtual ~Heap() = default;

  // Runs one atomic mark-sweep cycle. `stack_state` is accepted for API
  // compatibility but this port never scans the stack regardless of its
  // value (see file comment) -- pass kNoHeapPointers and keep every
  // reachable object rooted through a Persistent<T> or a traced Member<T>
  // edge, exactly like go++'s own Oilpan-lite already requires.
  void ForceGarbageCollectionSlow(
      const char* source, const char* reason,
      StackState stack_state = StackState::kNoHeapPointers);

  AllocationHandle& GetAllocationHandle();
  HeapHandle& GetHeapHandle();

 private:
  Heap() = default;
  friend class internal::Heap;
};

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_HEAP_H_
