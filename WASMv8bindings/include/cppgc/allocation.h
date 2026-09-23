// Simplified from V8's include/cppgc/allocation.h. Real cppgc's version
// also handles additional-bytes ("trailing array") allocations and custom
// spaces; both dropped here as explicit future work (see README) -- the
// core allocate-header-then-placement-new-T mechanism below is the real
// one, not a stand-in.
#ifndef WASMV8_INCLUDE_CPPGC_ALLOCATION_H_
#define WASMV8_INCLUDE_CPPGC_ALLOCATION_H_

#include <new>
#include <utility>

#include "cppgc/internal/gc-info.h"
#include "cppgc/type-traits.h"
#include "v8config.h"

namespace cppgc {

namespace internal {
class Heap;
}  // namespace internal

/**
 * Opaque handle used to allocate objects with `MakeGarbageCollected()`.
 * Acquired via `Heap::GetAllocationHandle()`.
 */
class V8_EXPORT AllocationHandle {
 private:
  AllocationHandle() = default;
  friend class internal::Heap;
};

namespace internal {

class V8_EXPORT MakeGarbageCollectedTraitInternal {
 protected:
  static void* Allocate(AllocationHandle&, size_t size, GCInfoIndex index);
  static void MarkObjectAsFullyConstructed(const void* payload);
};

template <typename T>
struct MakeGarbageCollectedTrait : public MakeGarbageCollectedTraitInternal {
  template <typename... Args>
  static T* Call(AllocationHandle& handle, Args&&... args) {
    static_assert(IsGarbageCollectedTypeV<T>,
                  "T must be a garbage collected type");
    void* memory =
        MakeGarbageCollectedTraitInternal::Allocate(handle, sizeof(T),
                                                     GCInfoTrait<T>::Index());
    T* object = ::new (memory) T(std::forward<Args>(args)...);
    MakeGarbageCollectedTraitInternal::MarkObjectAsFullyConstructed(object);
    return object;
  }
};

}  // namespace internal

/**
 * Allocates a garbage-collected object of type T and returns a pointer to it.
 */
template <typename T, typename... Args>
T* MakeGarbageCollected(AllocationHandle& handle, Args&&... args) {
  return internal::MakeGarbageCollectedTrait<T>::Call(
      handle, std::forward<Args>(args)...);
}

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_ALLOCATION_H_
