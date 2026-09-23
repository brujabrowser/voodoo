// Minimal usage example, the shape of real cppgc's own samples/cppgc/
// hello-world.cc.
#include <cstdio>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/visitor.h"

class Greeter final : public cppgc::GarbageCollected<Greeter> {
 public:
  void Trace(cppgc::Visitor*) const {}
  void Greet() const { std::printf("Hello from a cppgc-managed object!\n"); }
};

int main() {
  auto heap = cppgc::Heap::Create(nullptr);
  auto* greeter =
      cppgc::MakeGarbageCollected<Greeter>(heap->GetAllocationHandle());
  greeter->Greet();
  heap->ForceGarbageCollectionSlow("example", "shutdown");
  return 0;
}
