// Simplified from V8's include/cppgc/visitor.h.
//
// Real cppgc's Visitor also handles ephemerons (Trace(const EphemeronPair&)),
// custom weak callbacks (RegisterWeakCallback), and cross-thread/weak
// container traits. This port keeps the two shapes an embedder actually
// writes in a Trace() method day to day -- strong Member<T>/Persistent<T>
// edges, and WeakMember<T> edges cleared (not kept alive) by an unmarked
// target -- and drops ephemerons/custom weak callbacks as explicit future
// work (see README "What's not ported").
#ifndef WASMV8_INCLUDE_CPPGC_VISITOR_H_
#define WASMV8_INCLUDE_CPPGC_VISITOR_H_

#include "cppgc/internal/pointer-policies.h"
#include "cppgc/trace-trait.h"
#include "cppgc/type-traits.h"
#include "v8config.h"

namespace cppgc {

class V8_EXPORT Visitor {
 public:
  virtual ~Visitor() = default;

  // Strong Member<T> edge.
  template <typename T, typename WriteBarrierPolicy, typename CheckingPolicy,
            typename StorageType>
  void Trace(const internal::BasicMember<T, internal::StrongMemberTag,
                                         WriteBarrierPolicy, CheckingPolicy,
                                         StorageType>& member) {
    TraceStrong(member.Get());
  }

  // Weak WeakMember<T> edge: cleared (not kept alive) if unreachable
  // otherwise.
  template <typename T, typename WriteBarrierPolicy, typename CheckingPolicy,
            typename StorageType>
  void Trace(const internal::BasicMember<T, internal::WeakMemberTag,
                                         WriteBarrierPolicy, CheckingPolicy,
                                         StorageType>& weak_member) {
    using Member = internal::BasicMember<T, internal::WeakMemberTag,
                                         WriteBarrierPolicy, CheckingPolicy,
                                         StorageType>;
    TraceWeak(
        weak_member.Get(),
        [](void* slot) { static_cast<Member*>(slot)->ClearFromGC(); },
        const_cast<void*>(static_cast<const void*>(&weak_member)));
  }

  template <typename T>
  void TraceStrong(const T* object) {
    if (!object) return;
    static_assert(IsGarbageCollectedOrMixinTypeV<T>,
                  "T must be GarbageCollected or GarbageCollectedMixin");
    Visit(object, TraceTrait<T>::GetTraceDescriptor(object));
  }

  template <typename T>
  void TraceWeak(const T* object, void (*clear)(void* slot), void* slot) {
    if (!object) return;
    static_assert(IsGarbageCollectedOrMixinTypeV<T>,
                  "T must be GarbageCollected or GarbageCollectedMixin");
    VisitWeak(object, TraceTrait<T>::GetTraceDescriptor(object), clear, slot);
  }

 protected:
  virtual void Visit(const void* self, TraceDescriptor desc) = 0;
  virtual void VisitWeak(const void* self, TraceDescriptor desc,
                         void (*clear)(void* slot), void* slot) = 0;
};

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_VISITOR_H_
