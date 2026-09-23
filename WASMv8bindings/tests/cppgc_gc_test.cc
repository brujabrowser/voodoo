// Golden test: proves this is a real, working mark-sweep collector, not
// just API-shaped scaffolding -- a reachable chain survives an atomic GC, an
// unreachable tail gets actually finalized and freed, a WeakMember<T> edge
// is cleared (not kept alive) once its target is otherwise unreachable, and
// dropping the last Persistent<T> root collects the rest. Plain assertions,
// no test framework, matching every other WASM* sibling's golden-test style.
#include <cassert>
#include <cstdio>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/member.h"
#include "cppgc/persistent.h"
#include "cppgc/visitor.h"

namespace {

int g_live = 0;

class Node final : public cppgc::GarbageCollected<Node> {
 public:
  cppgc::Member<Node> next;
  cppgc::WeakMember<Node> weak_link;
  int id;

  explicit Node(int id) : id(id) { ++g_live; }
  ~Node() { --g_live; }

  void Trace(cppgc::Visitor* visitor) const {
    visitor->Trace(next);
    visitor->Trace(weak_link);
  }
};

}  // namespace

int main() {
  auto heap = cppgc::Heap::Create(nullptr);
  cppgc::AllocationHandle& handle = heap->GetAllocationHandle();

  cppgc::Persistent<Node> root = cppgc::MakeGarbageCollected<Node>(handle, 1);
  root->next = cppgc::MakeGarbageCollected<Node>(handle, 2);
  root->next->next = cppgc::MakeGarbageCollected<Node>(handle, 3);
  assert(g_live == 3);

  heap->ForceGarbageCollectionSlow("test", "reachable chain survives");
  assert(g_live == 3);

  root->next->next = nullptr;  // node 3 becomes unreachable
  heap->ForceGarbageCollectionSlow("test", "unreachable tail collected");
  assert(g_live == 2);

  {
    cppgc::Persistent<Node> temp = cppgc::MakeGarbageCollected<Node>(handle, 4);
    root->weak_link = temp.Get();
    assert(g_live == 3);
  }  // temp's only strong reference drops here
  assert(root->weak_link.Get() != nullptr);  // not cleared until a GC runs

  heap->ForceGarbageCollectionSlow("test", "weak edge cleared, target freed");
  assert(g_live == 2);
  assert(root->weak_link.Get() == nullptr);

  root = nullptr;  // drop the last root
  heap->ForceGarbageCollectionSlow("test", "root drop collects everything");
  assert(g_live == 0);

  heap.reset();
  std::printf("cppgc_gc_test: OK\n");
  return 0;
}
