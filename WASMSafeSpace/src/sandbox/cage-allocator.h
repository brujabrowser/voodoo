// std::allocator-compatible wrapper over Sandbox::Allocate()/Free() (see
// sandbox.h) -- lets an ordinary std::vector<T, CageAllocator<T>> (or any
// other allocator-aware container) genuinely live inside the cage instead
// of the plain process heap, without hand-rolling buffer management at
// every call site. The point: a container whose *identity* is protected
// by one of this repo's pointer tables (TrustedPointerTable/
// ExternalPointerTable/CppHeapPointerTable, all in this same src/sandbox/
// directory) but whose *backing bytes* still live in ordinary heap is
// only half-protected -- corruption reaching those bytes doesn't need to
// go through any table at all. CageAllocator is how a container closes
// that gap for data that's actually worth it (see the file comment on
// whichever high-churn buffer this gets applied to for the specific
// case).
//
// Falls back to plain ::operator new/delete when there's no active
// Sandbox (Sandbox::current() == nullptr) -- most tests and tools in this
// family never call Sandbox::Initialize() at all, and this allocator must
// stay usable there too, not just inside a cage-enabled process. A
// container that allocated while a Sandbox *was* current must not outlive
// that Sandbox's TearDown() -- same lifetime contract Free()'s own
// comment states (deallocate() re-checks Contains() at free time, so a
// torn-down-then-recreated Sandbox at a different address is detected as
// "not mine" and correctly falls back rather than corrupting the new
// cage, but a torn-down Sandbox with nothing active can't be detected,
// same as any other use-after-Sandbox-teardown).
#ifndef WASMSAFESPACE_SRC_SANDBOX_CAGE_ALLOCATOR_H_
#define WASMSAFESPACE_SRC_SANDBOX_CAGE_ALLOCATOR_H_

#include "src/sandbox/sandbox.h"

#include <cstddef>
#include <functional>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace v8 {
namespace internal {

template <typename T>
class CageAllocator {
 public:
  using value_type = T;

  CageAllocator() noexcept = default;
  template <typename U>
  CageAllocator(const CageAllocator<U>&) noexcept {}  // NOLINT

  T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    if (Sandbox* s = Sandbox::current()) {
      if (void* p = s->Allocate(n * sizeof(T), alignof(T))) {
        return static_cast<T*>(p);
      }
    }
    return static_cast<T*>(::operator new(n * sizeof(T)));
  }

  void deallocate(T* p, std::size_t n) noexcept {
    if (!p) return;
    if (Sandbox* s = Sandbox::current(); s && s->Contains(p)) {
      s->Free(p, n * sizeof(T));
      return;
    }
    ::operator delete(p);
  }
};

template <typename T, typename U>
inline bool operator==(const CageAllocator<T>&, const CageAllocator<U>&) {
  return true;
}
template <typename T, typename U>
inline bool operator!=(const CageAllocator<T>&, const CageAllocator<U>&) {
  return false;
}

// The one named spelling every repo in this family that passes wire-byte
// buffers across its own API boundary (WASMHolePunch's MessageObj::bytes,
// WASMCadidumKernel's ReadMessageRaw/WriteMessageRaw, WASMCadidumBindings'
// Connector) should use instead of each independently writing out
// std::vector<uint8_t, CageAllocator<uint8_t>> -- same benefit a shared
// type alias always has, plus it's the one place a future change to how
// these buffers are cage-backed only has to happen once.
using CageBytes = std::vector<uint8_t, CageAllocator<uint8_t>>;

// Allocator-aware containers for generated .voodoom array/map fields
// (WASMVoodooCompile) and any other occupancy-side table that needs its
// backing store in the cage. CageVector<T> is C++17-guaranteed to
// instantiate while T is still incomplete; CageMap<K, V> is not ISO-
// guaranteed (only vector/list/forward_list are) but is the occupancy
// spelling this family's toolchain actually ships -- libstdc++ 16 and
// libc++ 23 (wasi-sdk / go++ wasigocvm) both accept an incomplete V,
// empirically, which is what lets values.mojom's DictionaryValue hold
// map<string, Value> without Chromium's StructPtr box.
template <typename T>
using CageVector = std::vector<T, CageAllocator<T>>;

template <typename K, typename V, typename Hash = std::hash<K>,
          typename Eq = std::equal_to<K>>
using CageMap =
    std::unordered_map<K, V, Hash, Eq, CageAllocator<std::pair<const K, V>>>;

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_CAGE_ALLOCATOR_H_
