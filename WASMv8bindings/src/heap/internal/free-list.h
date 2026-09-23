// Strip-ported from V8's src/heap/cppgc-internal/free-list.h -- genuinely
// near-verbatim: the bucketed (by power-of-two size) freelist and its
// `Entry`/`Filler` design reuse `HeapObjectHeader` directly (a free entry
// literally IS a `HeapObjectHeader` tagged `kFreeListGCInfoIndex`), which
// has no OS/page-allocator dependency at all. `CollectStatistics` and
// `ContainsForTesting` are dropped (need `include/cppgc/heap-statistics.h`,
// a real-V8 introspection API not ported); `Append`/
// `AddReturningUnusedBounds` are dropped as unused by this port's simpler
// single-space allocator (see heap.h).
#ifndef WASMV8_SRC_HEAP_INTERNAL_FREE_LIST_H_
#define WASMV8_SRC_HEAP_INTERNAL_FREE_LIST_H_

#include <array>

#include "src/base/macros.h"
#include "src/heap/internal/globals.h"
#include "src/heap/internal/heap-object-header.h"

namespace cppgc {
namespace internal {

class Filler : public HeapObjectHeader {
 public:
  static Filler& CreateAt(void* memory, size_t size) {
    return *new (memory) Filler(size);
  }

 protected:
  explicit Filler(size_t size) : HeapObjectHeader(size, kFreeListGCInfoIndex) {}
};

class V8_EXPORT_PRIVATE FreeList {
 public:
  struct Block {
    void* address;
    size_t size;
  };

  FreeList() { Clear(); }
  FreeList(const FreeList&) = delete;
  FreeList& operator=(const FreeList&) = delete;

  // Allocates an entry of at least the given size, removing it from the
  // freelist. Returns {nullptr, 0} if no entry is large enough.
  Block Allocate(size_t allocation_size);

  // Adds a block to the freelist. The minimal block size is one word
  // (an unusable Filler entry); two words or more becomes a real,
  // reusable Entry.
  void Add(Block);

  void Clear();
  size_t Size() const;
  bool IsEmpty() const;

 private:
  class Entry;

  bool IsConsistent(size_t index) const;

  // All entries in the nth list have size >= 2^n.
  std::array<Entry*, kPageSizeLog2> free_list_heads_;
  std::array<Entry*, kPageSizeLog2> free_list_tails_;
  size_t biggest_free_list_index_ = 0;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_FREE_LIST_H_
