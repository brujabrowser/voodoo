// Strip-ported from V8's src/heap/cppgc-internal/free-list.cc -- see
// free-list.h's file comment for what's dropped (CollectStatistics,
// ContainsForTesting, Append, AddReturningUnusedBounds) and why. The core
// bucketed allocate/add/clear algorithm is unmodified.
#include "src/heap/internal/free-list.h"

#include <algorithm>

#include "src/base/bits.h"
#include "src/base/logging.h"

namespace cppgc {
namespace internal {

namespace {
uint32_t BucketIndexForSize(uint32_t size) {
  return v8::base::bits::WhichPowerOfTwo(
      v8::base::bits::RoundDownToPowerOfTwo32(size));
}
}  // namespace

class FreeList::Entry : public HeapObjectHeader {
 public:
  static Entry& CreateAt(void* memory, size_t size) {
    return *new (memory) Entry(size);
  }

  Entry* Next() const { return next_; }
  void SetNext(Entry* next) { next_ = next; }

  void Link(Entry** previous_next) {
    next_ = *previous_next;
    *previous_next = this;
  }
  void Unlink(Entry** previous_next) {
    *previous_next = next_;
    next_ = nullptr;
  }

 private:
  explicit Entry(size_t size) : HeapObjectHeader(size, kFreeListGCInfoIndex) {
    static_assert(sizeof(Entry) == kFreeListEntrySize, "Sizes must match");
  }

  Entry* next_ = nullptr;
};

void FreeList::Add(FreeList::Block block) {
  const size_t size = block.size;
  DCHECK_GT(kPageSize, size);
  DCHECK_LE(sizeof(HeapObjectHeader), size);

  if (size < sizeof(Entry)) {
    // Wasted entry -- can happen when an almost-emptied linear allocation
    // buffer is returned to the freelist. Still a valid, walkable
    // HeapObjectHeader (just not reusable for allocation).
    Filler::CreateAt(block.address, size);
    return;
  }

  Entry& entry = Entry::CreateAt(block.address, size);
  const size_t index = BucketIndexForSize(static_cast<uint32_t>(size));
  entry.Link(&free_list_heads_[index]);
  biggest_free_list_index_ = std::max(biggest_free_list_index_, index);
  if (!entry.Next()) {
    free_list_tails_[index] = &entry;
  }
}

FreeList::Block FreeList::Allocate(size_t allocation_size) {
  // Try reusing a block from the largest bin: amortizes this slow
  // allocation call by carving off as large a free block as possible in one
  // go, servicing many subsequent allocations via fast bump allocation.
  size_t bucket_size = static_cast<size_t>(1) << biggest_free_list_index_;
  size_t index = biggest_free_list_index_;
  for (; index > 0; --index, bucket_size >>= 1) {
    DCHECK(IsConsistent(index));
    Entry* entry = free_list_heads_[index];
    if (allocation_size > bucket_size) {
      // Final bucket candidate; only check the head entry, no linear scan.
      if (!entry || entry->AllocatedSize() < allocation_size) break;
    }
    if (entry) {
      if (!entry->Next()) {
        free_list_tails_[index] = nullptr;
      }
      entry->Unlink(&free_list_heads_[index]);
      biggest_free_list_index_ = index;
      return {entry, entry->AllocatedSize()};
    }
  }
  biggest_free_list_index_ = index;
  return {nullptr, 0u};
}

void FreeList::Clear() {
  std::fill(free_list_heads_.begin(), free_list_heads_.end(), nullptr);
  std::fill(free_list_tails_.begin(), free_list_tails_.end(), nullptr);
  biggest_free_list_index_ = 0;
}

size_t FreeList::Size() const {
  size_t size = 0;
  for (auto* entry : free_list_heads_) {
    while (entry) {
      size += entry->AllocatedSize();
      entry = entry->Next();
    }
  }
  return size;
}

bool FreeList::IsEmpty() const {
  return std::all_of(free_list_heads_.cbegin(), free_list_heads_.cend(),
                     [](const auto* entry) { return !entry; });
}

bool FreeList::IsConsistent(size_t index) const {
  return (!free_list_heads_[index] && !free_list_tails_[index]) ||
         (free_list_heads_[index] && free_list_tails_[index] &&
          !free_list_tails_[index]->Next());
}

}  // namespace internal
}  // namespace cppgc
