#include "src/heap/internal/heap.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#include "cppgc/internal/pointer-policies.h"
#include "cppgc/default-platform.h"
#include "cppgc/platform.h"
#include "src/base/logging.h"
#include "src/heap/internal/globals.h"
#include "src/heap/internal/marking-visitor.h"

namespace cppgc {

// ---- Public cppgc::Heap facade -------------------------------------------
// Heap's real constructor is private (friend internal::Heap); these free
// functions/methods are the only code allowed to build one.

std::unique_ptr<Heap> Heap::Create(std::shared_ptr<Platform> platform,
                                   HeapOptions options) {
  CHECK(options.stack_support == StackSupport::kNoConservativeStackScan);
  CHECK(options.marking_support == MarkingType::kAtomic);
  CHECK(options.sweeping_support == SweepingType::kAtomic);
  if (!platform) platform = std::make_shared<DefaultPlatform>();
  return std::make_unique<internal::Heap>(std::move(platform));
}

void Heap::ForceGarbageCollectionSlow(const char*, const char*, StackState) {
  internal::Heap::From(GetHeapHandle()).CollectGarbage();
}

AllocationHandle& Heap::GetAllocationHandle() {
  return static_cast<internal::Heap&>(*this);
}

HeapHandle& Heap::GetHeapHandle() {
  return static_cast<internal::Heap&>(*this);
}

namespace internal {

void* MakeGarbageCollectedTraitInternal::Allocate(AllocationHandle& handle,
                                                  size_t size,
                                                  GCInfoIndex index) {
  return Heap::From(handle).Allocate(size, index);
}

void MakeGarbageCollectedTraitInternal::MarkObjectAsFullyConstructed(
    const void* payload) {
  const_cast<HeapObjectHeader&>(HeapObjectHeader::FromObject(payload))
      .MarkAsFullyConstructed();
}

}  // namespace internal
}  // namespace cppgc

// ---- internal::Heap --------------------------------------------------

namespace cppgc {
namespace internal {

namespace {
Heap* g_active_heap = nullptr;

size_t RoundUp(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}
}  // namespace

PersistentRegion& StrongPersistentPolicy::GetPersistentRegion(const void*) {
  CHECK(g_active_heap);
  return g_active_heap->GetStrongPersistentRegion();
}

PersistentRegion& WeakPersistentPolicy::GetPersistentRegion(const void*) {
  CHECK(g_active_heap);
  return g_active_heap->GetWeakPersistentRegion();
}

Heap::Heap(std::shared_ptr<Platform> platform)
    : platform_(std::move(platform)), oom_handler_(this) {
  CHECK(!g_active_heap);  // v1: exactly one live Heap at a time, see heap.h.
  g_active_heap = this;
  if (!IsInitialized()) {
    InitializeProcess(platform_ ? platform_->GetPageAllocator() : nullptr);
  }
}

Heap::~Heap() {
  // Process shutdown: finalize everything still alive, regardless of
  // reachability -- matches real cppgc's heap-teardown finalization pass.
  // Flush the LAB into the freelist first so every page is a fully
  // walkable chain of real HeapObjectHeaders (see SweepNormalPage's
  // comment) rather than ending in unformatted bump space.
  ReplaceLinearAllocationBuffer(nullptr, 0);
  for (NormalPage* page : normal_pages_) {
    Address current = page->PayloadStart();
    Address const end = page->PayloadEnd();
    while (current < end) {
      HeapObjectHeader* header = reinterpret_cast<HeapObjectHeader*>(current);
      const size_t size = header->AllocatedSize();
      if (!header->IsFree()) header->Finalize();
      current += size;
    }
    NormalPage::Destroy(page);
  }
  normal_pages_.clear();

  for (HeapObjectHeader* header : large_objects_) {
    header->Finalize();
    delete[] reinterpret_cast<uint8_t*>(header);
  }
  large_objects_.clear();

  g_active_heap = nullptr;
}

void* Heap::Allocate(size_t object_size, GCInfoIndex gc_info_index) {
  const size_t allocation_size =
      RoundUp(sizeof(HeapObjectHeader) + object_size, kAllocationGranularity);
  CHECK_LE(allocation_size, HeapObjectHeader::kMaxSize);
  if (allocation_size >= kLargeObjectSizeThreshold) {
    return AllocateLarge(allocation_size, gc_info_index);
  }
  return AllocateNormal(allocation_size, gc_info_index);
}

void* Heap::AllocateLarge(size_t allocation_size, GCInfoIndex gc_info_index) {
  uint8_t* base = new (std::nothrow) uint8_t[allocation_size];
  if (!base) oom_handler_("Oilpan: large allocation.");
  HeapObjectHeader* header =
      new (base) HeapObjectHeader(allocation_size, gc_info_index);
  large_objects_.push_back(header);
  return header->ObjectStart();
}

void* Heap::AllocateNormal(size_t allocation_size, GCInfoIndex gc_info_index) {
  if (lab_.size() < allocation_size) {
    if (!RefillLinearAllocationBuffer(allocation_size)) {
      // Try to make room and retry once before giving up -- matches real
      // cppgc's GarbageCollector::RetryAllocate idiom
      // (third_party/v8/src/heap/cppgc-internal/object-allocator.cc).
      CollectGarbage();
      if (!RefillLinearAllocationBuffer(allocation_size)) {
        oom_handler_("Oilpan: normal allocation.");
      }
    }
  }
  Address raw = lab_.Allocate(allocation_size);
  HeapObjectHeader* header =
      new (raw) HeapObjectHeader(allocation_size, gc_info_index);
  return header->ObjectStart();
}

bool Heap::RefillLinearAllocationBuffer(size_t size) {
  const FreeList::Block block = free_list_.Allocate(size);
  if (block.address) {
    ReplaceLinearAllocationBuffer(static_cast<Address>(block.address),
                                  block.size);
    return true;
  }
  // Sandbox::current() is null unless an embedder has explicitly called
  // v8::internal::Sandbox::set_current() -- the default, unsandboxed
  // behavior (plain new[] page payloads) is unchanged for every existing
  // caller that never touches Sandbox at all. See normal-page.h's file
  // comment for what changes when a sandbox is active.
  NormalPage* page = NormalPage::Create(v8::internal::Sandbox::current());
  if (!page) return false;
  normal_pages_.push_back(page);
  ReplaceLinearAllocationBuffer(page->PayloadStart(), page->PayloadSize());
  return true;
}

void Heap::ReplaceLinearAllocationBuffer(Address new_start, size_t new_size) {
  // The old LAB's leftover bytes become a real, walkable free-list Entry
  // (FreeList::Add placement-news a HeapObjectHeader into that memory) --
  // this is what lets SweepNormalPage walk a page as one uninterrupted
  // chain of headers with no separate "skip the LAB" case, unlike real
  // cppgc's NormalPage::iterator (see that file's comment in
  // third_party/v8/src/heap/cppgc-internal/heap-page.h).
  if (lab_.size() > 0) {
    free_list_.Add({lab_.start(), lab_.size()});
  }
  lab_.Set(new_start, new_size);
}

size_t Heap::SweepNormalPage(NormalPage* page,
                             std::vector<FreeList::Block>& free_runs) {
  // Free runs are only staged here, not added to `free_list_` directly:
  // if this page turns out fully empty (return value 0), the caller
  // destroys it immediately, and every address in `free_runs` would
  // dangle. Only a page that survives gets its runs actually added (see
  // CollectGarbage) -- getting this backwards is a real, previously-shipped
  // bug in this port (use-after-free via a freelist entry pointing into an
  // already-`delete[]`'d page), not a hypothetical one.
  Address current = page->PayloadStart();
  Address const end = page->PayloadEnd();
  Address free_run_start = nullptr;
  size_t live_bytes = 0;
  while (current < end) {
    HeapObjectHeader* header = reinterpret_cast<HeapObjectHeader*>(current);
    const size_t size = header->AllocatedSize();
    const bool is_free = header->IsFree();
    if (is_free || !header->IsMarked()) {
      if (!is_free) header->Finalize();
      if (!free_run_start) free_run_start = current;
    } else {
      header->Unmark();
      live_bytes += size;
      if (free_run_start) {
        free_runs.push_back(
            {free_run_start, static_cast<size_t>(current - free_run_start)});
        free_run_start = nullptr;
      }
    }
    current += size;
  }
  if (free_run_start) {
    free_runs.push_back({free_run_start, static_cast<size_t>(end - free_run_start)});
  }
  return live_bytes;
}

void Heap::Mark() {
  MarkingVisitor visitor;

  // Root marking: every live Persistent<T>/WeakPersistent<T>. Strong roots
  // feed straight into the worklist; weak roots are recorded for the
  // second pass below.
  strong_persistents_.Iterate(visitor);
  visitor.RunToFixpoint();
  weak_persistents_.Iterate(visitor);
  // A weak persistent's target may have been reached (and so marked) via
  // some other strong path already -- iterating it can't discover new
  // strong work, only queue its own weak edge, so no further
  // RunToFixpoint() call is needed here.
  visitor.ProcessWeakEdges();
}

void Heap::Sweep() {
  // Sweep normal pages: flush the LAB so every page is fully walkable (see
  // ReplaceLinearAllocationBuffer's comment), rebuild the freelist from
  // scratch as the walk rediscovers both previously-free and newly-dead
  // ranges, and drop any page that ends up completely empty.
  ReplaceLinearAllocationBuffer(nullptr, 0);
  free_list_.Clear();
  std::vector<NormalPage*> surviving_pages;
  surviving_pages.reserve(normal_pages_.size());
  for (NormalPage* page : normal_pages_) {
    std::vector<FreeList::Block> free_runs;
    const size_t live_bytes = SweepNormalPage(page, free_runs);
    if (live_bytes == 0) {
      NormalPage::Destroy(page);
    } else {
      for (const FreeList::Block& run : free_runs) free_list_.Add(run);
      surviving_pages.push_back(page);
    }
  }
  normal_pages_.swap(surviving_pages);

  // Sweep large objects: reclaim everything left unmarked, unmark
  // survivors for the next cycle.
  std::vector<HeapObjectHeader*> surviving_large;
  surviving_large.reserve(large_objects_.size());
  for (HeapObjectHeader* header : large_objects_) {
    if (header->IsMarked()) {
      header->Unmark();
      surviving_large.push_back(header);
    } else {
      header->Finalize();
      delete[] reinterpret_cast<uint8_t*>(header);
    }
  }
  large_objects_.swap(surviving_large);
}

class Heap::GcJob final : public JobTask {
 public:
  GcJob(Heap* heap, void (Heap::*phase)()) : heap_(heap), phase_(phase) {}

  void Run(JobDelegate* delegate) override {
    if (delegate && delegate->ShouldYield()) return;
    (heap_->*phase_)();
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return worker_count < 1 ? 1 : 0;
  }

 private:
  Heap* heap_;
  void (Heap::*phase_)();
};

void Heap::RunGcJob(void (Heap::*phase)()) {
  CHECK(platform_);
  auto handle = platform_->PostJob(TaskPriority::kUserBlocking,
                                   std::make_unique<GcJob>(this, phase));
  if (handle) {
    handle->Join();
  } else {
    (this->*phase)();
  }
}

void Heap::CollectGarbage() {
  RunGcJob(&Heap::Mark);
  RunGcJob(&Heap::Sweep);
}

}  // namespace internal
}  // namespace cppgc
