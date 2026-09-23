// Golden test for ExternalPointerTable: the same tag-range-checked-
// indirection mechanism as TrustedPointerTable (trusted_pointer_table_test.cc)
// applied to raw external (non-V8-object) pointers -- e.g. what a real
// ArrayBuffer backing-store allocator or an embedder-managed resource
// handle would go through. See external-pointer-table.h's file comment for
// why this table uses its own, independently-chosen bit layout rather than
// reusing TrustedPointerTable's.
#include <cassert>
#include <cstdio>

#include "src/sandbox/external-pointer-table.h"

using v8::internal::ExternalPointerHandle;
using v8::internal::ExternalPointerTable;
using v8::internal::ExternalPointerTag;
using v8::internal::ExternalPointerTagRange;

namespace {
constexpr ExternalPointerTag kBackingStoreTag =
    static_cast<ExternalPointerTag>(1);
constexpr ExternalPointerTag kManagedResourceTag =
    static_cast<ExternalPointerTag>(2);
}  // namespace

int main() {
  ExternalPointerTable table;

  int backing_store = 1, managed_resource = 2, other = 3;

  ExternalPointerHandle backing_handle =
      table.AllocateAndInitializeEntry(&backing_store, kBackingStoreTag);
  ExternalPointerHandle managed_handle =
      table.AllocateAndInitializeEntry(&managed_resource, kManagedResourceTag);

  assert(table.Get(backing_handle, kBackingStoreTag) == &backing_store);
  assert(table.Get(managed_handle, kManagedResourceTag) == &managed_resource);

  // Type confusion is rejected with a clean nullptr.
  assert(table.Get(managed_handle, kBackingStoreTag) == nullptr);
  assert(table.Get(backing_handle, kManagedResourceTag) == nullptr);

  // Null / out-of-range handles are rejected the same way.
  assert(table.Get(ExternalPointerTable::kNullHandle, kBackingStoreTag) == nullptr);
  assert(table.Get(static_cast<ExternalPointerHandle>(9999), kBackingStoreTag) ==
         nullptr);

  // A wide range check (e.g. "any managed pointer") accepts either tag.
  ExternalPointerTagRange any(kBackingStoreTag, kManagedResourceTag);
  assert(table.Get(backing_handle, any) == &backing_store);
  assert(table.Get(managed_handle, any) == &managed_resource);

  // Set() overwrites in place.
  table.Set(backing_handle, &other, kBackingStoreTag);
  assert(table.Get(backing_handle, kBackingStoreTag) == &other);

  // Free + reuse.
  ExternalPointerHandle other_handle =
      table.AllocateAndInitializeEntry(&other, kManagedResourceTag);
  assert(table.Contains(other_handle));
  table.FreeEntry(other_handle);
  assert(!table.Contains(other_handle));
  size_t size_before_reuse = table.SizeForTesting();
  ExternalPointerHandle reused =
      table.AllocateAndInitializeEntry(&backing_store, kBackingStoreTag);
  assert(table.SizeForTesting() == size_before_reuse);
  assert(reused == other_handle);
  assert(table.Get(reused, kBackingStoreTag) == &backing_store);

  // Compact(): five entries, free a middle "hole" then a genuine trailing
  // run; only the trailing run actually shrinks the table (same pattern as
  // trusted_pointer_table_test.cc -- see external-entity-table.h's
  // Compact() comment for why a hole with a live entry after it can't be
  // trimmed without a mark/relocation pass this port deliberately doesn't
  // have).
  ExternalPointerTable ct;
  auto h0 = ct.AllocateAndInitializeEntry(&backing_store, kBackingStoreTag);
  auto h1 = ct.AllocateAndInitializeEntry(&managed_resource, kManagedResourceTag);
  auto h2 = ct.AllocateAndInitializeEntry(&other, kManagedResourceTag);
  auto h3 = ct.AllocateAndInitializeEntry(&backing_store, kBackingStoreTag);
  auto h4 = ct.AllocateAndInitializeEntry(&managed_resource, kManagedResourceTag);
  assert(ct.SizeForTesting() == 5);

  ct.FreeEntry(h1);
  ct.Compact();
  assert(ct.SizeForTesting() == 5);  // h1's hole isn't trailing (h2 follows it)
  assert(!ct.Contains(h1));

  ct.FreeEntry(h3);
  ct.FreeEntry(h4);
  ct.Compact();
  assert(ct.SizeForTesting() == 3);  // h0, h1's hole, h2 remain
  assert(ct.Get(h0, kBackingStoreTag) == &backing_store);
  assert(ct.Get(h2, kManagedResourceTag) == &other);
  assert(!ct.Contains(h3));
  assert(!ct.Contains(h4));

  std::printf("external_pointer_table_test: OK\n");
  return 0;
}
