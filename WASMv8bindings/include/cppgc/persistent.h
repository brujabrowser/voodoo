// Simplified from V8's include/cppgc/persistent.h (real file also covers
// cross-thread persistents, unsupported here -- see persistent-node.h's
// file comment for why that's a deliberate, documented cut for a
// single-heap, single-thread port).
//
// One deviation from real cppgc worth flagging explicitly: real
// BasicPersistent re-derives its PersistentRegion from the pointee's page on
// every free, because a multi-heap embedder's Persistent<T> could target any
// heap. This port caches the PersistentRegion* at construction time instead
// (see region_ below) so that freeing a WeakPersistent<T> whose target the
// GC already cleared doesn't need a non-null value to re-derive anything
// from -- correct here specifically because this port supports exactly one
// live Heap (see heap.h).
#ifndef WASMV8_INCLUDE_CPPGC_PERSISTENT_H_
#define WASMV8_INCLUDE_CPPGC_PERSISTENT_H_

#include <cstddef>
#include <type_traits>
#include <utility>

#include "cppgc/internal/persistent-node.h"
#include "cppgc/internal/pointer-policies.h"
#include "cppgc/source-location.h"
#include "cppgc/visitor.h"
#include "v8config.h"

namespace cppgc {
namespace internal {

template <typename T, typename WeaknessPolicy,
          typename LocationPolicy, typename CheckingPolicy>
class BasicPersistent : private LocationPolicy, private CheckingPolicy {
 public:
  using PointeeType = T;

  explicit BasicPersistent(const SourceLocation& loc = SourceLocation::Current())
      : LocationPolicy(loc) {}
  BasicPersistent(std::nullptr_t,
                  const SourceLocation& loc = SourceLocation::Current())
      : LocationPolicy(loc) {}
  BasicPersistent(T* raw,  // NOLINT(runtime/explicit)
                  const SourceLocation& loc = SourceLocation::Current())
      : LocationPolicy(loc) {
    Assign(raw);
  }
  BasicPersistent(T& raw,  // NOLINT(runtime/explicit)
                  const SourceLocation& loc = SourceLocation::Current())
      : BasicPersistent(&raw, loc) {}

  BasicPersistent(const BasicPersistent& other,
                  const SourceLocation& loc = SourceLocation::Current())
      : BasicPersistent(other.Get(), loc) {}
  template <typename U, typename OtherWeakness, typename OtherLocation,
            typename OtherChecking,
            typename = std::enable_if_t<std::is_base_of_v<T, U>>>
  BasicPersistent(  // NOLINT(runtime/explicit)
      const BasicPersistent<U, OtherWeakness, OtherLocation, OtherChecking>&
          other,
      const SourceLocation& loc = SourceLocation::Current())
      : BasicPersistent(other.Get(), loc) {}

  BasicPersistent(BasicPersistent&& other) noexcept
      : LocationPolicy(std::move(other)),
        value_(other.value_),
        node_(other.node_),
        region_(other.region_) {
    if (node_) node_->UpdateOwner(this);
    other.value_ = nullptr;
    other.node_ = nullptr;
    other.region_ = nullptr;
  }

  ~BasicPersistent() { Clear(); }

  BasicPersistent& operator=(const BasicPersistent& other) {
    Assign(other.Get());
    return *this;
  }
  BasicPersistent& operator=(T* raw) {
    Assign(raw);
    return *this;
  }
  BasicPersistent& operator=(std::nullptr_t) {
    Clear();
    return *this;
  }

  T* Get() const { return value_; }
  void Clear() {
    if (node_) region_->FreeNode(node_);
    value_ = nullptr;
    node_ = nullptr;
    region_ = nullptr;
  }
  T* Release() {
    T* result = Get();
    Clear();
    return result;
  }

  explicit operator bool() const { return value_ != nullptr; }
  operator T*() const { return value_; }
  T* operator->() const { return value_; }
  T& operator*() const { return *value_; }

  using IsStrongPersistent = typename WeaknessPolicy::IsStrongPersistent;

 private:
  void Assign(T* raw) {
    if (node_) region_->FreeNode(node_);
    value_ = raw;
    node_ = nullptr;
    region_ = nullptr;
    if (raw) {
      this->CheckPointer(raw);
      region_ = &WeaknessPolicy::GetPersistentRegion(raw);
      node_ = region_->AllocateNode(this, &BasicPersistent::Trace);
    }
  }

  // Cleared by Visitor's weak-clear callback once sweeping determines the
  // pointee is unreachable. The node stays registered (and keeps tracing a
  // now-null value_, a no-op) until this BasicPersistent is itself
  // destroyed or reassigned -- matches real cppgc's weak-persistent
  // lifetime, see class comment.
  void ClearFromGC() { value_ = nullptr; }

  static void Trace(Visitor& visitor, const void* object) {
    auto* persistent = const_cast<BasicPersistent*>(
        static_cast<const BasicPersistent*>(object));
    if constexpr (WeaknessPolicy::IsStrongPersistent::value) {
      visitor.TraceStrong(persistent->Get());
    } else {
      visitor.TraceWeak(
          persistent->Get(),
          [](void* self) {
            static_cast<BasicPersistent*>(self)->ClearFromGC();
          },
          persistent);
    }
  }

  T* value_ = nullptr;
  PersistentNode* node_ = nullptr;
  PersistentRegion* region_ = nullptr;
};

}  // namespace internal

template <typename T>
using Persistent =
    internal::BasicPersistent<T, internal::StrongPersistentPolicy>;

template <typename T>
using WeakPersistent =
    internal::BasicPersistent<T, internal::WeakPersistentPolicy>;

}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_PERSISTENT_H_
