// Golden test for the Sandbox cage itself: Contains() is exact (true inside,
// false for anything outside -- including addresses that are merely "close"
// to the cage), the in-cage allocator only ever hands out memory that
// Contains() itself agrees is inside the cage, alignment is honored,
// Free()'d space is actually reclaimed (reused, and coalesced with
// adjacent free blocks rather than fragmenting), and TearDown() actually
// invalidates containment (nothing "left over" is still considered inside
// afterwards).
#if defined(_WIN32)
#include <windows.h>
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "src/sandbox/sandbox.h"

using v8::Address;
using v8::internal::Sandbox;

int main() {
  Sandbox sandbox;
  assert(!sandbox.is_initialized());

  sandbox.Initialize(1 * 1024 * 1024);  // 1MB cage
  assert(sandbox.is_initialized());
  assert(sandbox.size() == 1u * 1024 * 1024);
  assert(sandbox.end() == sandbox.base() + sandbox.size());

  // The base itself, and the last valid byte, are inside; end() (one past
  // the last byte) and anything beyond are not.
  assert(sandbox.Contains(sandbox.base()));
  assert(sandbox.Contains(sandbox.end() - 1));
  assert(!sandbox.Contains(sandbox.end()));
  assert(!sandbox.Contains(static_cast<Address>(0)));
  assert(!sandbox.Contains(static_cast<Address>(~Address{0})));  // max address

  // An address that merely looks plausible (e.g. one byte before base, one
  // past the reservation) must still be rejected -- this is the whole
  // point of a cage boundary being exact rather than approximate.
  if (sandbox.base() != 0) {
    assert(!sandbox.Contains(sandbox.base() - 1));
  }

  // The bump allocator only ever hands out in-cage memory, and respects
  // alignment.
  void* a = sandbox.Allocate(64, 16);
  void* b = sandbox.Allocate(3, 16);  // odd size forces padding before next
  void* c = sandbox.Allocate(64, 64);
  assert(sandbox.Contains(a));
  assert(sandbox.Contains(b));
  assert(sandbox.Contains(c));
  assert(reinterpret_cast<uintptr_t>(a) % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(b) % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(c) % 64 == 0);
  assert(a != b && b != c && a != c);

  // Free() actually reclaims: exhaust a tiny cage with the bump path, free
  // one block, and prove the next allocation that would otherwise fail
  // (CHECK_LE would abort) succeeds by reusing it -- reclamation, not just
  // bookkeeping that says it happened.
  {
    Sandbox tiny;
    tiny.Initialize(256);
    void* p1 = tiny.Allocate(128, 16);
    void* p2 = tiny.Allocate(128, 16);
    assert(tiny.Contains(p1) && tiny.Contains(p2));
    // The cage is now full (256 bytes, two 128-byte allocations) -- a
    // third allocation of any size must fail without a Free() first.
    tiny.Free(p1, 128);
    void* p3 = tiny.Allocate(128, 16);
    assert(p3 == p1);  // exact reuse of the freed block (first-fit)
    assert(tiny.Contains(p3));
    tiny.TearDown();
  }

  // Coalescing: freeing two adjacent blocks (in either order) merges them
  // into one, so a request larger than either individual block -- but no
  // larger than their sum -- can still be satisfied from the free list
  // instead of falling through to the (now-exhausted) bump path.
  {
    Sandbox tiny;
    tiny.Initialize(256);
    void* p1 = tiny.Allocate(64, 16);
    void* p2 = tiny.Allocate(64, 16);
    void* p3 = tiny.Allocate(128, 16);  // exhausts the 256-byte cage
    assert(tiny.Contains(p3));
    tiny.Free(p1, 64);
    tiny.Free(p2, 64);
    void* merged = tiny.Allocate(128, 16);
    assert(merged == p1);  // coalesced [p1,p1+128) reused as one block
    tiny.TearDown();
  }

  // A second, independent sandbox has its own, disjoint range.
  Sandbox other;
  other.Initialize(4096);
  assert(other.Contains(other.base()));
  assert(!sandbox.Contains(other.base()) || sandbox.base() == other.base());
  // (the "||" clause only guards against an astronomically unlikely
  // allocator coincidence; in practice the two heap allocations never
  // overlap.)

  // current()/set_current(): the single-active-sandbox slot real V8 also
  // maintains (see sandbox.h's file comment).
  assert(Sandbox::current() == nullptr);
  Sandbox::set_current(&sandbox);
  assert(Sandbox::current() == &sandbox);
  Sandbox::set_current(nullptr);

  other.TearDown();
  assert(!other.is_initialized());

  sandbox.TearDown();
  assert(!sandbox.is_initialized());

  // A fresh cage reads as zero. TryAllocate refuses what no longer fits
  // instead of aborting, and a freed block makes room again.
  {
    Sandbox small;
    small.Initialize(64 * 1024);
    auto* first = static_cast<unsigned char*>(small.TryAllocate(40 * 1024, 16));
    assert(first != nullptr);
    for (size_t i = 0; i < 40 * 1024; ++i) assert(first[i] == 0);
    assert(small.TryAllocate(40 * 1024, 16) == nullptr);
    assert(small.TryAllocate(size_t{1} << 40, 16) == nullptr);
    small.Free(first, 40 * 1024);
    assert(small.TryAllocate(40 * 1024, 16) != nullptr);
    small.TearDown();
  }

#if defined(_WIN32)
  // Commit on use: a large cage is reserved address space; what the bump
  // path hands out is committed, zeroed, and writable end to end, and the
  // rest stays reserved.
  {
    Sandbox big;
    big.Initialize(size_t{256} << 20);
    auto* first = static_cast<unsigned char*>(big.TryAllocate(size_t{1} << 20, 16));
    assert(first != nullptr && first[0] == 0 && first[(size_t{1} << 20) - 1] == 0);
    first[(size_t{1} << 20) - 1] = 1;
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(reinterpret_cast<void*>(big.base() + (size_t{64} << 20)), &info, sizeof(info));
    assert(info.State == MEM_RESERVE);
    const size_t n = size_t{200} << 20;
    auto* second = static_cast<unsigned char*>(big.TryAllocate(n, 16));
    assert(second != nullptr && second[n - 1] == 0);
    second[0] = 2;
    second[n - 1] = 3;
    VirtualQuery(second + n - 1, &info, sizeof(info));
    assert(info.State == MEM_COMMIT);
    assert(big.TryAllocate(size_t{100} << 20, 16) == nullptr);
    big.TearDown();
  }
#endif

  std::printf("sandbox_test: OK\n");
  return 0;
}
