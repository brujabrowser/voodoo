// Simplified from V8's include/cppgc/member.h (the real file is ~700 lines,
// covering compressed/cross-thread/tagged variants and a write-barrier fast
// path). This keeps the shape an embedder actually names -- Member<T>,
// WeakMember<T>, UntracedMember<T> -- backed by the same BasicMember<T,
// WeaknessTag, WriteBarrierPolicy, CheckingPolicy, StorageType> template
// real cppgc's type-traits.h pattern-matches on (IsMemberTypeV<T> etc. work
// unmodified against this).
#ifndef WASMV8_INCLUDE_CPPGC_MEMBER_H_
#define WASMV8_INCLUDE_CPPGC_MEMBER_H_

#include <cstddef>
#include <type_traits>

#include "cppgc/internal/member-storage.h"
#include "cppgc/internal/pointer-policies.h"
#include "cppgc/sentinel-pointer.h"
#include "cppgc/type-traits.h"
#include "v8config.h"

namespace cppgc {
namespace internal {

template <typename T, typename WeaknessTag, typename WriteBarrierPolicy,
          typename CheckingPolicy, typename StorageType>
class BasicMember : private CheckingPolicy {
 public:
  using PointeeType = T;

  constexpr BasicMember() = default;
  constexpr BasicMember(std::nullptr_t) {}
  BasicMember(SentinelPointer s) : storage_(s) {}
  BasicMember(T* raw) : storage_(raw) {
    WriteBarrierPolicy::InitializingBarrier(&storage_, raw);
    this->CheckPointer(raw);
  }
  BasicMember(T& raw) : BasicMember(&raw) {}

  BasicMember(const BasicMember& other) : BasicMember(other.Get()) {}
  template <typename U, typename OtherBarrier, typename OtherChecking,
            typename OtherStorage,
            typename = std::enable_if_t<std::is_base_of_v<T, U>>>
  BasicMember(  // NOLINT(runtime/explicit)
      const BasicMember<U, WeaknessTag, OtherBarrier, OtherChecking,
                        OtherStorage>& other)
      : BasicMember(other.Get()) {}

  BasicMember& operator=(const BasicMember& other) {
    return operator=(other.Get());
  }
  BasicMember& operator=(T* raw) {
    storage_.Store(raw);
    WriteBarrierPolicy::template AssigningBarrier<
        StorageType::kWriteBarrierSlotType>(&storage_, storage_);
    this->CheckPointer(raw);
    return *this;
  }
  BasicMember& operator=(std::nullptr_t) {
    Clear();
    return *this;
  }

  T* Get() const { return const_cast<T*>(static_cast<const T*>(storage_.Load())); }
  void Clear() { storage_.Clear(); }
  T* Release() {
    T* result = Get();
    Clear();
    return result;
  }

  explicit operator bool() const { return !storage_.IsCleared(); }
  operator T*() const { return Get(); }
  T* operator->() const { return Get(); }
  T& operator*() const { return *Get(); }

  // Invoked by Visitor's weak-clear callback once sweeping determines the
  // pointee is unreachable.
  void ClearFromGC() { storage_.Clear(); }

 private:
  StorageType storage_;
};

}  // namespace internal

template <typename T>
using Member = internal::BasicMember<T, internal::StrongMemberTag,
                                     internal::DijkstraWriteBarrierPolicy>;

template <typename T>
using WeakMember = internal::BasicMember<T, internal::WeakMemberTag,
                                         internal::DijkstraWriteBarrierPolicy>;

template <typename T>
using UntracedMember = internal::BasicMember<T, internal::UntracedMemberTag,
                                             internal::NoWriteBarrierPolicy>;

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_MEMBER_H_
