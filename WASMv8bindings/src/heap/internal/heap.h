// This port's own Heap -- not a strip-port of any single upstream file,
// but now driven by real strip-ported pieces: HeapObjectHeader (bit
// layout), GCInfoTable (trace/finalize dispatch), and FreeList (bucketed
// reuse) all come from src/heap/internal/*, strip-ported from real V8
// source -- see each of those files' own header comments. Real cppgc's
// HeapBase/Heap machinery additionally wires up NormalPageSpace/
// LargePageSpace (this port has one implicit normal space plus a flat
// large-object list, not cppgc's several size-classed spaces), a Marker
// with concurrent worker threads, an incremental/concurrent Sweeper, and a
// Compactor -- this port implements only the atomic (stop-the-world),
// non-incremental, non-concurrent, non-compacting, precise-only subset,
// one of cppgc's own documented first-class configurations (see the
// top-level README).
//
// Allocation: a bump-pointer linear allocation buffer (LAB) over a
// NormalPage (src/heap/internal/normal-page.h, kPageSize per page,
// matching real cppgc), refilled from the FreeList or a freshly allocated
// page when it runs dry, matching real cppgc's actual
// ObjectAllocator::AllocateObjectOnSpace fast path
// (third_party/v8/src/heap/cppgc-internal/object-allocator.h). Objects at
// or above kLargeObjectSizeThreshold skip the page/LAB path entirely and
// get one dedicated `new[]` block each, same as real cppgc's LargePage
// (just without a distinct LargePage wrapper type -- the HeapObjectHeader
// pointer alone is enough bookkeeping here).
#ifndef WASMV8_SRC_HEAP_INTERNAL_HEAP_H_
#define WASMV8_SRC_HEAP_INTERNAL_HEAP_H_

#include <memory>
#include <vector>

#include "cppgc/allocation.h"
#include "cppgc/heap-handle.h"
#include "cppgc/heap.h"
#include "cppgc/internal/gc-info.h"
#include "cppgc/internal/persistent-node.h"
#include "cppgc/platform.h"
#include "src/heap/internal/free-list.h"
#include "src/heap/internal/heap-object-header.h"
#include "src/heap/internal/normal-page.h"
#include "src/heap/internal/platform.h"

namespace cppgc {
namespace internal {

// A bump-pointer allocation region carved out of one NormalPage. Matches
// real cppgc's NormalPageSpace::LinearAllocationBuffer shape.
class LinearAllocationBuffer final {
 public:
  Address start() const { return start_; }
  size_t size() const { return size_; }

  void Set(Address start, size_t size) {
    start_ = start;
    size_ = size;
  }

  Address Allocate(size_t allocation_size) {
    DCHECK_LE(allocation_size, size_);
    Address result = start_;
    start_ += allocation_size;
    size_ -= allocation_size;
    return result;
  }

 private:
  Address start_ = nullptr;
  size_t size_ = 0;
};

class Heap final : public cppgc::Heap,
                   public cppgc::AllocationHandle,
                   public cppgc::HeapHandle,
                   public HeapBase {
 public:
  static Heap& From(cppgc::AllocationHandle& handle) {
    return static_cast<Heap&>(handle);
  }
  static Heap& From(cppgc::HeapHandle& handle) {
    return static_cast<Heap&>(handle);
  }

  explicit Heap(std::shared_ptr<Platform> platform);
  ~Heap();

  void* Allocate(size_t object_size, GCInfoIndex gc_info_index);

  // Atomic mark-sweep over the whole heap. See file comment: no stack scan,
  // no incremental/concurrent phases, no compaction. Mark and sweep each
  // run as a Platform::PostJob on the calling thread.
  void CollectGarbage();

  PersistentRegion& GetStrongPersistentRegion() { return strong_persistents_; }
  PersistentRegion& GetWeakPersistentRegion() { return weak_persistents_; }

  // White-box accessors for tests/cppgc_allocator_test.cc -- verifying
  // multi-page allocation, freelist reuse, and empty-page reclamation
  // actually happen isn't observable through the public cppgc:: API alone.
  size_t NormalPageCountForTesting() const { return normal_page_count(); }
  size_t LargeObjectCountForTesting() const { return large_object_count(); }
  size_t FreeListSizeForTesting() const { return free_list_.Size(); }

  size_t normal_page_count() const override { return normal_pages_.size(); }
  size_t large_object_count() const override { return large_objects_.size(); }

 private:
  void* AllocateNormal(size_t allocation_size, GCInfoIndex gc_info_index);
  void* AllocateLarge(size_t allocation_size, GCInfoIndex gc_info_index);

  // Refills `lab_` from the freelist or a freshly allocated page. Returns
  // false only on real allocation failure (OOM).
  bool RefillLinearAllocationBuffer(size_t size);
  void ReplaceLinearAllocationBuffer(Address new_start, size_t new_size);

  // Walks one page header-by-header, finalizing dead objects and staging
  // coalesced dead/free runs into `free_runs` (NOT added to `free_list_`
  // directly -- see the .cc file comment on why). Returns the page's
  // surviving byte count (0 == fully empty, caller should destroy the
  // page rather than trust any of the staged runs).
  size_t SweepNormalPage(NormalPage* page, std::vector<FreeList::Block>& free_runs);

  class GcJob;
  void Mark();
  void Sweep();
  void RunGcJob(void (Heap::*phase)());

  std::shared_ptr<Platform> platform_;
  FatalOutOfMemoryHandler oom_handler_;

  std::vector<NormalPage*> normal_pages_;
  FreeList free_list_;
  LinearAllocationBuffer lab_;

  std::vector<HeapObjectHeader*> large_objects_;

  PersistentRegion strong_persistents_;
  PersistentRegion weak_persistents_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_HEAP_H_
