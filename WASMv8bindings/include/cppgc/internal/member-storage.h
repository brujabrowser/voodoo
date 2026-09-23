// Adapted from V8's include/cppgc/internal/member-storage.h.
//
// Real cppgc's CompressedPointer shifts+truncates a 64-bit heap pointer into
// a cage-relative uint32_t, then reconstructs the full address by
// sign-extending and ANDing with a cage base whose low bits are all 1
// (see the upstream file for the bit trick). That reconstruction is only
// correct when the cage base is non-zero and reservation-aligned -- both
// guaranteed by a real OS mmap reservation.
//
// wasm32-wasip1 has no such reservation: linear memory starts at address 0
// and every native pointer already fits in 32 bits. So "the cage" here is
// simply the whole linear memory (base 0, no shift needed) and compression
// degenerates to the identity function on the pointer bits. This mirrors
// how real V8 behaves on its own 32-bit hosts (ia32): CPPGC_POINTER_COMPRESSION
// is a 64-bit-host-only feature there too, for exactly this reason. We keep
// CompressedPointer as a distinct type from RawPointer anyway, matching the
// real API shape (Member<T>'s storage type), so call sites and a future
// 64-bit host port aren't forced to change shape later.
#ifndef WASMV8_INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_
#define WASMV8_INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_

#include <atomic>
#include <cstdint>

#include "cppgc/sentinel-pointer.h"
#include "v8config.h"

namespace cppgc {
namespace internal {

enum class WriteBarrierSlotType {
  kCompressed,
  kUncompressed,
};

class V8_TRIVIAL_ABI CompressedPointer final {
 public:
  struct AtomicInitializerTag {};

  using IntegralType = uint32_t;
  static constexpr auto kWriteBarrierSlotType =
      WriteBarrierSlotType::kCompressed;

  V8_INLINE CompressedPointer() : value_(0u) {}
  V8_INLINE explicit CompressedPointer(const void* value, AtomicInitializerTag) {
    StoreAtomic(value);
  }
  V8_INLINE explicit CompressedPointer(const void* ptr) : value_(Compress(ptr)) {}
  V8_INLINE explicit CompressedPointer(std::nullptr_t) : value_(0u) {}
  V8_INLINE explicit CompressedPointer(SentinelPointer)
      : value_(kCompressedSentinel) {}

  V8_INLINE const void* Load() const { return Decompress(value_); }
  V8_INLINE const void* LoadAtomic() const {
    return Decompress(
        reinterpret_cast<const std::atomic<IntegralType>&>(value_).load(
            std::memory_order_relaxed));
  }

  V8_INLINE void Store(const void* ptr) { value_ = Compress(ptr); }
  V8_INLINE void StoreAtomic(const void* value) {
    reinterpret_cast<std::atomic<IntegralType>&>(value_).store(
        Compress(value), std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { value_ = 0u; }
  V8_INLINE bool IsCleared() const { return !value_; }
  V8_INLINE bool IsSentinel() const { return value_ == kCompressedSentinel; }
  V8_INLINE uint32_t GetAsInteger() const { return value_; }

  V8_INLINE friend bool operator==(CompressedPointer a, CompressedPointer b) {
    return a.value_ == b.value_;
  }
  V8_INLINE friend bool operator!=(CompressedPointer a, CompressedPointer b) {
    return a.value_ != b.value_;
  }

  static V8_INLINE IntegralType Compress(const void* ptr) {
    // Identity: wasm32 pointers already fit in 32 bits. See file comment.
    return static_cast<IntegralType>(reinterpret_cast<uintptr_t>(ptr));
  }
  static V8_INLINE void* Decompress(IntegralType ptr) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(ptr));
  }

 private:
  static constexpr IntegralType kCompressedSentinel =
      static_cast<IntegralType>(SentinelPointer::kSentinelValue);
  IntegralType value_;
};

class V8_TRIVIAL_ABI RawPointer final {
 public:
  struct AtomicInitializerTag {};

  using IntegralType = uintptr_t;
  static constexpr auto kWriteBarrierSlotType =
      WriteBarrierSlotType::kUncompressed;

  V8_INLINE RawPointer() : ptr_(nullptr) {}
  V8_INLINE explicit RawPointer(const void* ptr, AtomicInitializerTag) {
    StoreAtomic(ptr);
  }
  V8_INLINE explicit RawPointer(const void* ptr) : ptr_(ptr) {}

  V8_INLINE const void* Load() const { return ptr_; }
  V8_INLINE const void* LoadAtomic() const {
    return reinterpret_cast<const std::atomic<const void*>&>(ptr_).load(
        std::memory_order_relaxed);
  }

  V8_INLINE void Store(const void* ptr) { ptr_ = ptr; }
  V8_INLINE void StoreAtomic(const void* ptr) {
    reinterpret_cast<std::atomic<const void*>&>(ptr_).store(
        ptr, std::memory_order_relaxed);
  }

  V8_INLINE void Clear() { ptr_ = nullptr; }
  V8_INLINE bool IsCleared() const { return !ptr_; }
  V8_INLINE bool IsSentinel() const { return ptr_ == kSentinelPointer; }
  V8_INLINE uintptr_t GetAsInteger() const {
    return reinterpret_cast<uintptr_t>(ptr_);
  }

  V8_INLINE friend bool operator==(RawPointer a, RawPointer b) {
    return a.ptr_ == b.ptr_;
  }
  V8_INLINE friend bool operator!=(RawPointer a, RawPointer b) {
    return a.ptr_ != b.ptr_;
  }

 private:
  const void* ptr_;
};

// Real cppgc defaults to CompressedPointer only when CPPGC_POINTER_COMPRESSION
// is defined (64-bit hosts). We default to RawPointer here: on a 32-bit
// target compression buys nothing (see file comment) and RawPointer is one
// indirection simpler. CompressedPointer stays available/compiled (and unit
// tested) for a future 64-bit host.
using DefaultMemberStorage = RawPointer;

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_INTERNAL_MEMBER_STORAGE_H_
