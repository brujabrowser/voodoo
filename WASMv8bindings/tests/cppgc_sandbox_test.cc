// Golden test proving cppgc's NormalPage allocations actually land inside a
// real v8::internal::Sandbox cage when one is active
// (src/heap/internal/normal-page.h's Sandbox::Allocate() path) -- the
// composition both this repo's and ../../WASMSafeSpace's READMEs name as
// the previously-missing wiring between cppgc and the Sandbox. Mirrors
// tests/cppgc_allocator_test.cc's white-box multi-page shape, plus a
// Contains() assertion on every allocated object.
#include <cassert>
#include <cstdio>
#include <vector>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/persistent.h"
#include "cppgc/visitor.h"
#include "src/heap/internal/heap.h"
#include "src/sandbox/sandbox.h"

namespace {

class Node final : public cppgc::GarbageCollected<Node> {
 public:
  char payload[64];  // big enough that a few thousand of these span pages
  void Trace(cppgc::Visitor*) const {}
};

}  // namespace

int main() {
  v8::internal::Sandbox sandbox;
  sandbox.Initialize(8 * 1024 * 1024);  // 8MB -- plenty for this test's page count
  v8::internal::Sandbox::set_current(&sandbox);

  {
    auto heap = cppgc::Heap::Create(nullptr);
    cppgc::AllocationHandle& handle = heap->GetAllocationHandle();
    cppgc::internal::Heap& internal_heap = cppgc::internal::Heap::From(handle);

    assert(internal_heap.NormalPageCountForTesting() == 0);

    constexpr int kCount = 2000;  // enough to span multiple 128KiB pages
    std::vector<cppgc::Persistent<Node>> roots;
    roots.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
      Node* node = cppgc::MakeGarbageCollected<Node>(handle);
      // Every object, checked right at allocation time...
      assert(sandbox.Contains(node));
      roots.emplace_back(node);
    }
    assert(internal_heap.NormalPageCountForTesting() > 1);  // really spans pages

    // ...and every object again, walked back through its Persistent<T> root,
    // proving it's not just the most-recently-bump-allocated one that
    // happens to land in range.
    for (auto& root : roots) {
      assert(sandbox.Contains(root.Get()));
    }

    // A GC cycle (which sweeps/destroys empty NormalPages) must not crash
    // or misbehave just because those pages were sandbox-backed.
    roots.resize(10);
    heap->ForceGarbageCollectionSlow("test", "reclaim most pages");
    for (auto& root : roots) {
      assert(sandbox.Contains(root.Get()));
    }

    roots.clear();
    heap->ForceGarbageCollectionSlow("test", "drop everything");
  }

  v8::internal::Sandbox::set_current(nullptr);
  sandbox.TearDown();

  std::printf("cppgc_sandbox_test: OK\n");
  return 0;
}
