// Golden test for the CppHeapPointerTable indirect-handle mechanism: proves
// tag-range checking actually rejects a type-confused access (returns
// nullptr, never the raw pointer), that a matching tag/range succeeds, that
// freed entries are reused via the freelist without handles aliasing while
// still live, and that Compact() actually shrinks the backing storage
// rather than just marking capacity reusable.
#include <cassert>
#include <cstdio>

#include "src/sandbox/cppheap-pointer-table.h"

using cppgc::internal::CppHeapPointerTable;
using v8::CppHeapPointerHandle;
using v8::CppHeapPointerTag;
using v8::CppHeapPointerTagRange;

namespace {
// A toy type hierarchy's tag assignment, following the post-order scheme
// described in include/v8-sandbox.h: leaves first, each supertype's range
// exactly spans its subtypes.
//
//        Node
//       /    \
//   Element  Text
//
constexpr CppHeapPointerTag kElementTag = static_cast<CppHeapPointerTag>(1);
constexpr CppHeapPointerTag kTextTag = static_cast<CppHeapPointerTag>(2);
constexpr CppHeapPointerTagRange kNodeTagRange(kElementTag, kTextTag);
constexpr CppHeapPointerTag kUnrelatedTag = static_cast<CppHeapPointerTag>(3);
}  // namespace

int main() {
  CppHeapPointerTable table;

  int element_obj = 1, text_obj = 2, unrelated_obj = 3;

  CppHeapPointerHandle element_handle =
      table.AllocateAndInitializeEntry(&element_obj, kElementTag);
  CppHeapPointerHandle text_handle =
      table.AllocateAndInitializeEntry(&text_obj, kTextTag);
  CppHeapPointerHandle unrelated_handle =
      table.AllocateAndInitializeEntry(&unrelated_obj, kUnrelatedTag);

  // Exact-tag lookups succeed.
  assert(table.Get(element_handle, kElementTag) == &element_obj);
  assert(table.Get(text_handle, kTextTag) == &text_obj);

  // A supertype's range check accepts either subtype.
  assert(table.Get(element_handle, kNodeTagRange) == &element_obj);
  assert(table.Get(text_handle, kNodeTagRange) == &text_obj);

  // A type-confused access -- asking for a Node but the handle is actually
  // the unrelated type -- is rejected with a clean nullptr, not the raw
  // pointer.
  assert(table.Get(unrelated_handle, kNodeTagRange) == nullptr);

  // Null and out-of-range handles are rejected the same safe way.
  assert(table.Get(CppHeapPointerTable::kNullHandle, kNodeTagRange) == nullptr);
  assert(table.Get(static_cast<CppHeapPointerHandle>(9999), kNodeTagRange) ==
        nullptr);

  // Freeing then reallocating reuses the slot but the old handle no longer
  // resolves to anything (it's now tagged as the new entry, whatever that
  // turns out to be) -- exercised here by checking Contains() drops
  // immediately on free.
  assert(table.Contains(unrelated_handle));
  table.FreeEntry(unrelated_handle);
  assert(!table.Contains(unrelated_handle));
  size_t size_before_reuse = table.SizeForTesting();
  CppHeapPointerHandle reused =
      table.AllocateAndInitializeEntry(&element_obj, kElementTag);
  assert(table.SizeForTesting() == size_before_reuse);  // reused the freed slot
  assert(reused == unrelated_handle);
  assert(table.Get(reused, kElementTag) == &element_obj);

  // Untouched handles remain valid throughout.
  assert(table.Get(element_handle, kElementTag) == &element_obj);
  assert(table.Get(text_handle, kTextTag) == &text_obj);

  // Compact(): trailing freed entries actually shrink the table, not just
  // become reusable capacity. Five entries: h1 will become an isolated
  // "hole" (freed early, but h2 stays live right after it, blocking it
  // from ever being trailing); h3/h4 become the genuine trailing run.
  CppHeapPointerTable compact_table;
  CppHeapPointerHandle h0 = compact_table.AllocateAndInitializeEntry(&element_obj, kElementTag);
  CppHeapPointerHandle h1 = compact_table.AllocateAndInitializeEntry(&text_obj, kTextTag);
  CppHeapPointerHandle h2 =
      compact_table.AllocateAndInitializeEntry(&unrelated_obj, kUnrelatedTag);
  CppHeapPointerHandle h3 = compact_table.AllocateAndInitializeEntry(&element_obj, kElementTag);
  CppHeapPointerHandle h4 = compact_table.AllocateAndInitializeEntry(&text_obj, kTextTag);
  assert(compact_table.SizeForTesting() == 5);

  // Free the middle hole (h1) and confirm Compact() leaves the table size
  // alone -- it only trims a *trailing* run of free entries, deliberately
  // never relocating still-live entries (h2, h3, h4) to close a gap (see
  // the header's file comment: no mark phase, no relocation).
  compact_table.FreeEntry(h1);
  compact_table.Compact();
  assert(compact_table.SizeForTesting() == 5);  // h1's hole isn't trailing
  assert(!compact_table.Contains(h1));
  assert(compact_table.Get(h2, kUnrelatedTag) == &unrelated_obj);

  // Now free the actual trailing run (h3, h4) and confirm those -- and
  // only those -- get reclaimed; h1's still-open hole in the middle
  // remains exactly what it was, and h2 (the live entry separating the
  // hole from the trailing run) is untouched.
  compact_table.FreeEntry(h3);
  compact_table.FreeEntry(h4);
  compact_table.Compact();
  assert(compact_table.SizeForTesting() == 3);  // h0, h1's hole, h2 remain
  assert(compact_table.Get(h0, kElementTag) == &element_obj);
  assert(compact_table.Get(h2, kUnrelatedTag) == &unrelated_obj);
  assert(!compact_table.Contains(h1));
  assert(!compact_table.Contains(h3));
  assert(!compact_table.Contains(h4));

  // The freelist survives compaction correctly: a fresh allocation reuses
  // h1's still-open hole rather than growing the table (no corrupted
  // freelist chain left dangling from the trimmed trailing indices).
  const size_t size_before_reuse2 = compact_table.SizeForTesting();
  CppHeapPointerHandle h5 = compact_table.AllocateAndInitializeEntry(&text_obj, kTextTag);
  assert(h5 == h1);
  assert(compact_table.SizeForTesting() == size_before_reuse2);
  assert(compact_table.Get(h5, kTextTag) == &text_obj);

  std::printf("cppheap_pointer_table_test: OK\n");
  return 0;
}
