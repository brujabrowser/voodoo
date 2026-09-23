// This port's ExternalEntityTable<Entry>: the shared freelist/allocation
// base that TrustedPointerTable, CodePointerTable, and ExternalPointerTable
// below all derive from -- factored out once here instead of copy-pasted
// three times, same motivation as real V8's own src/sandbox/
// external-entity-table.h (vendored at third_party/v8/src/sandbox/
// external-entity-table.h), which is the exact same base class shared by
// upstream's four real external-entity tables (TrustedPointerTable,
// CodePointerTable, ExternalPointerTable, CppHeapPointerTable).
//
// Real ExternalEntityTable partitions its backing memory into fixed-size
// Segments (typically 64kb) carved out of one large OS-reserved virtual
// address space region, grouped into Spaces that each own a freelist, so
// that entry allocation/freeing/compaction can be scoped per-space (e.g.
// young-generation vs. old-generation entries) and segments can be
// individually returned to the OS. This port has no OS reservation to
// segment (see the top-level README and ../WASMv8bindings' own precedent
// in src/sandbox/cppheap-pointer-table.h, which this class generalizes):
// there is exactly one implicit space, backed by one plain
// `std::vector<Entry>`, with a classic freelist threaded through freed
// entries via `Entry::ExtractFreelistLink()`. The real security property
// -- a freed/out-of-range/wrong-tagged handle can never resolve to a live
// pointer of another type -- is fully preserved (each concrete table's
// `Get()` still range-checks the index and tag-checks the entry); what's
// dropped is the memory-management machinery (segment-level reclamation,
// multiple spaces, OS-page compaction), not the safety mechanism itself.
//
// `Entry` must provide:
//   - `Entry::MakeFreelistEntry(uint32_t next_free_index)` (static factory)
//   - `bool ContainsFreelistLink() const`
//   - `std::optional<uint32_t> ExtractFreelistLink() const`
// Every concrete Entry type below (TrustedPointerTableEntry,
// CodePointerTableEntry, ExternalPointerTableEntry) implements these on top
// of its own TaggedPayload.
#ifndef WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
#define WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_ENTITY_TABLE_H_

#include <cstdint>
#include <vector>

#include "src/base/logging.h"

namespace v8 {
namespace internal {

template <typename Entry>
class ExternalEntityTable {
 public:
  ExternalEntityTable() = default;
  ExternalEntityTable(const ExternalEntityTable&) = delete;
  ExternalEntityTable& operator=(const ExternalEntityTable&) = delete;

  static constexpr uint32_t kNullIndex = ~uint32_t{0};

  size_t SizeForTesting() const { return entries_.size(); }
  bool IsInBounds(uint32_t index) const { return index < entries_.size(); }

 protected:
  // Allocates a new entry (reusing a freed slot if the freelist is
  // non-empty) and returns its index. `init` is invoked with a reference to
  // the (uninitialized-or-reused) entry so the caller can install real
  // content before the slot is observable by anyone else -- single
  // threaded, so no atomicity concerns unlike upstream's concurrent
  // freelist CAS loop.
  template <typename InitFn>
  uint32_t AllocateEntry(InitFn init) {
    uint32_t index;
    if (free_list_head_ != kNullIndex) {
      index = free_list_head_;
      auto next = entries_[index].ExtractFreelistLink();
      DCHECK(next.has_value());
      free_list_head_ = *next;
    } else {
      index = static_cast<uint32_t>(entries_.size());
      entries_.emplace_back();
    }
    init(entries_[index]);
    return index;
  }

  void FreeEntry(uint32_t index) {
    DCHECK(IsInBounds(index));
    entries_[index] = Entry::MakeFreelistEntry(free_list_head_);
    free_list_head_ = index;
  }

  Entry& at(uint32_t index) {
    DCHECK(IsInBounds(index));
    return entries_[index];
  }
  const Entry& at(uint32_t index) const {
    DCHECK(IsInBounds(index));
    return entries_[index];
  }

  // This port's version of real SweepAndCompact's bounded-memory property
  // (not its mechanism -- see ../WASMv8bindings/src/sandbox/
  // cppheap-pointer-table.h's file comment for why trimming a trailing run
  // needs no mark phase here: liveness is always already known from the
  // freelist). Trims trailing freelist entries off the backing vector and
  // rebuilds the freelist without the trimmed indices. Live entries that
  // remain, and their handles, are unaffected.
  void Compact() {
    while (!entries_.empty() && entries_.back().ContainsFreelistLink()) {
      entries_.pop_back();
    }
    uint32_t new_size = static_cast<uint32_t>(entries_.size());
    uint32_t new_head = kNullIndex;
    // Rebuild is O(n) but only over the freelist, and only ever needed once
    // per Compact() call -- rebuild rather than trying to splice the old
    // (now-partially-invalid) freelist chain, since some interior freelist
    // links may have pointed at the trimmed trailing run.
    for (uint32_t i = 0; i < new_size; ++i) {
      if (entries_[i].ContainsFreelistLink()) {
        entries_[i] = Entry::MakeFreelistEntry(new_head);
        new_head = i;
      }
    }
    free_list_head_ = new_head;
  }

  std::vector<Entry> entries_;
  uint32_t free_list_head_ = kNullIndex;
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_ENTITY_TABLE_H_
