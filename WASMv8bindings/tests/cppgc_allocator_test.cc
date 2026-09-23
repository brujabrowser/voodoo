// Golden test for the real page/bump-pointer/FreeList allocator
// (src/heap/internal/{normal-page,free-list,heap}.*), replacing this
// port's original one-`new[]`-per-object design. Unlike
// tests/cppgc_gc_test.cc (pure public-API), this one reaches into
// src/heap/internal/heap.h directly -- a deliberate white-box test, since
// "did this actually span multiple pages / actually reclaim an empty page
// / actually reuse freed space" isn't observable through the public
// cppgc:: API alone.
#include <cassert>
#include <cstdio>
#include <vector>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/persistent.h"
#include "cppgc/visitor.h"
#include "src/heap/internal/heap.h"

namespace {

int g_live = 0;

class Node final : public cppgc::GarbageCollected<Node> {
 public:
  char payload[64];  // big enough that a few thousand of these span pages
  Node() { ++g_live; }
  ~Node() { --g_live; }
  void Trace(cppgc::Visitor*) const {}
};

}  // namespace

int main() {
  auto heap = cppgc::Heap::Create(nullptr);
  cppgc::AllocationHandle& handle = heap->GetAllocationHandle();
  cppgc::internal::Heap& internal_heap = cppgc::internal::Heap::From(handle);

  assert(internal_heap.NormalPageCountForTesting() == 0);

  constexpr int kCount = 5000;
  std::vector<cppgc::Persistent<Node>> roots;
  roots.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    roots.push_back(cppgc::MakeGarbageCollected<Node>(handle));
  }
  assert(g_live == kCount);
  const size_t pages_after_alloc = internal_heap.NormalPageCountForTesting();
  assert(pages_after_alloc > 1);  // really did span multiple pages

  // Everything is still reachable: a GC must not lose anything, and with
  // nothing dead there's nothing to reclaim.
  heap->ForceGarbageCollectionSlow("test", "all reachable");
  assert(g_live == kCount);
  assert(internal_heap.NormalPageCountForTesting() == pages_after_alloc);

  // Drop all but a handful of roots.
  constexpr int kSurvivors = 10;
  roots.resize(kSurvivors);
  heap->ForceGarbageCollectionSlow("test", "most become unreachable");
  assert(g_live == kSurvivors);

  // Real reclamation, not just bookkeeping: with only 10 survivors out of
  // thousands of objects, at least one now-fully-empty page must have
  // actually been destroyed.
  assert(internal_heap.NormalPageCountForTesting() < pages_after_alloc);

  // A fresh batch of the same size should be serviced by freelist/reclaimed
  // -page reuse rather than needing dramatically more pages than the very
  // first allocation pass did.
  for (int i = 0; i < kCount; ++i) {
    roots.push_back(cppgc::MakeGarbageCollected<Node>(handle));
  }
  assert(g_live == kSurvivors + kCount);
  heap->ForceGarbageCollectionSlow("test", "sanity GC after reuse batch");
  assert(g_live == kSurvivors + kCount);
  assert(internal_heap.NormalPageCountForTesting() <= pages_after_alloc + 2);

  roots.clear();
  heap->ForceGarbageCollectionSlow("test", "drop everything");
  assert(g_live == 0);
  assert(internal_heap.NormalPageCountForTesting() == 0);

  heap.reset();
  std::printf("cppgc_allocator_test: OK\n");
  return 0;
}
