// This port's Visitor implementation, driving one atomic mark phase. Real
// cppgc splits this across MarkingVisitorBase/MutatorMarkingVisitor plus a
// separate MarkingWorklists with per-thread segments for concurrent marking
// (see src/heap/cppgc-internal/marking-visitor.*, marking-worklists.*).
// This port is single-threaded and atomic-only (see heap.h), so one
// worklist and one visitor instance covers the whole mark phase.
#ifndef WASMV8_SRC_HEAP_INTERNAL_MARKING_VISITOR_H_
#define WASMV8_SRC_HEAP_INTERNAL_MARKING_VISITOR_H_

#include <vector>

#include "cppgc/trace-trait.h"
#include "cppgc/visitor.h"
#include "src/heap/internal/heap-object-header.h"

namespace cppgc {
namespace internal {

class MarkingVisitor final : public Visitor {
 public:
  // Marks the whole transitive closure reachable from every strong root
  // already queued via Visit() (persistent roots feed in through
  // PersistentRegion::Iterate before this runs).
  void RunToFixpoint() {
    while (!worklist_.empty()) {
      TraceDescriptor desc = worklist_.back();
      worklist_.pop_back();
      desc.callback(this, desc.base_object_payload);
    }
  }

  // Second pass, after RunToFixpoint(): clears every weak edge (WeakMember<T>
  // or WeakPersistent<T>) whose target didn't get marked above. Must run
  // after strong marking reaches fixpoint -- a weak edge recorded early
  // during marking may point at an object only *later* reached via a
  // different strong path, so it can't be judged dead until marking is
  // completely done. Real cppgc calls this "weak processing"; see its
  // Oilpan README's "Marking phase" section (vendored under
  // third_party/v8/include/cppgc/README.md).
  void ProcessWeakEdges() {
    for (auto& entry : weak_edges_) {
      const HeapObjectHeader& header = HeapObjectHeader::FromObject(entry.target);
      if (!header.IsMarked()) {
        entry.clear(entry.slot);
      }
    }
    weak_edges_.clear();
  }

 protected:
  void Visit(const void*, TraceDescriptor desc) override {
    HeapObjectHeader& header = const_cast<HeapObjectHeader&>(
        HeapObjectHeader::FromObject(desc.base_object_payload));
    if (header.TryMarkAtomic()) {
      worklist_.push_back(desc);
    }
  }

  void VisitWeak(const void*, TraceDescriptor desc,
                void (*clear)(void* slot), void* slot) override {
    weak_edges_.push_back({desc.base_object_payload, clear, slot});
  }

 private:
  struct WeakEdge {
    const void* target;
    void (*clear)(void* slot);
    void* slot;
  };

  std::vector<TraceDescriptor> worklist_;
  std::vector<WeakEdge> weak_edges_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_MARKING_VISITOR_H_
