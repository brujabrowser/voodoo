// Adapted from V8's include/cppgc/internal/pointer-policies.h.
//
// Real cppgc's DijkstraWriteBarrierPolicy emits a marking/generational write
// barrier so incremental/concurrent marking stays sound while the mutator
// runs concurrently with the collector. This port only supports
// Heap::MarkingType::kAtomic (stop-the-world, see heap.h) -- there is no
// marking phase running concurrently with mutator writes for a barrier to
// protect, so both write-barrier policies below are genuinely no-ops, not
// stubs standing in for missing functionality. If incremental marking is
// ever added, this is exactly the file that grows a real barrier.
//
// SameThreadEnabledCheckingPolicy (real cppgc's CPPGC_ENABLE_SLOW_API_CHECKS
// path) is not ported: it walks BasePage/PageBackend to verify a pointer
// belongs to the same heap, both part of the page-allocator machinery this
// port replaces with a simple arena (see src/heap/object-allocator.h).
// DisabledCheckingPolicy is what real cppgc itself defaults to when that
// build flag is off, so this is the real default behavior, not a cut corner.
#ifndef WASMV8_INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_
#define WASMV8_INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_

#include <type_traits>

#include "cppgc/internal/member-storage.h"
#include "cppgc/sentinel-pointer.h"
#include "cppgc/source-location.h"
#include "cppgc/type-traits.h"
#include "v8config.h"

namespace cppgc {
namespace internal {

class PersistentRegion;

// Tags to distinguish between strong and weak member types.
class StrongMemberTag;
class WeakMemberTag;
class UntracedMemberTag;

struct DijkstraWriteBarrierPolicy {
  V8_INLINE static void InitializingBarrier(const void*, const void*) {}
  V8_INLINE static void InitializingBarrier(const void*, RawPointer) {}
  template <WriteBarrierSlotType>
  V8_INLINE static void AssigningBarrier(const void*, const void*) {}
  template <WriteBarrierSlotType, typename MemberStorage>
  V8_INLINE static void AssigningBarrier(const void*, MemberStorage) {}
};

struct NoWriteBarrierPolicy {
  V8_INLINE static void InitializingBarrier(const void*, const void*) {}
  V8_INLINE static void InitializingBarrier(const void*, RawPointer) {}
  template <WriteBarrierSlotType>
  V8_INLINE static void AssigningBarrier(const void*, const void*) {}
  template <WriteBarrierSlotType, typename MemberStorage>
  V8_INLINE static void AssigningBarrier(const void*, MemberStorage) {}
};

class DisabledCheckingPolicy {
 protected:
  template <typename T>
  V8_INLINE void CheckPointer(T*) {}
  template <typename T>
  V8_INLINE void CheckPointer(RawPointer) {}
};

using DefaultMemberCheckingPolicy = DisabledCheckingPolicy;
using DefaultPersistentCheckingPolicy = DisabledCheckingPolicy;

class IgnoreLocationPolicy {
 public:
  constexpr SourceLocation Location() const { return {}; }

 protected:
  constexpr IgnoreLocationPolicy() = default;
  constexpr explicit IgnoreLocationPolicy(SourceLocation) {}
};

using DefaultLocationPolicy = IgnoreLocationPolicy;

struct StrongPersistentPolicy {
  using IsStrongPersistent = std::true_type;
  static V8_EXPORT PersistentRegion& GetPersistentRegion(const void* object);
};

struct WeakPersistentPolicy {
  using IsStrongPersistent = std::false_type;
  static V8_EXPORT PersistentRegion& GetPersistentRegion(const void* object);
};

// Forward declarations setting up the default policies -- matches real
// cppgc's type-traits.h expectations (IsMemberTypeV etc. pattern-match on
// these exact template parameter lists).
template <typename T, typename WeaknessPolicy,
          typename LocationPolicy = DefaultLocationPolicy,
          typename CheckingPolicy = DefaultPersistentCheckingPolicy>
class BasicPersistent;
template <typename T, typename WeaknessTag, typename WriteBarrierPolicy,
          typename CheckingPolicy = DefaultMemberCheckingPolicy,
          typename StorageType = DefaultMemberStorage>
class BasicMember;

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_INTERNAL_POINTER_POLICIES_H_
