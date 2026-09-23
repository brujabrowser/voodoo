// Golden test for TrustedPointerTable: exact-tag and tag-range lookups
// succeed, a type-confused access returns a clean kNullAddress (never the
// real pointer), freed entries are reused via the freelist, and Compact()
// trims only a genuine trailing run (see external-entity-table.h and
// ../WASMv8bindings/src/sandbox/cppheap-pointer-table.h for why that's the
// right, mark-phase-free notion of compaction here).
#include <cassert>
#include <cstdio>

#include "src/sandbox/trusted-pointer-table.h"

using v8::Address;
using v8::internal::IndirectPointerTag;
using v8::internal::IndirectPointerTagRange;
using v8::internal::kCodeIndirectPointerTag;
using v8::internal::kGenericTrustedObjectTag;
using v8::internal::TrustedPointerHandle;
using v8::internal::TrustedPointerTable;

int main() {
  TrustedPointerTable table;

  int code_obj = 1, trusted_obj = 2, other_obj = 3;
  Address code_addr = reinterpret_cast<Address>(&code_obj);
  Address trusted_addr = reinterpret_cast<Address>(&trusted_obj);
  Address other_addr = reinterpret_cast<Address>(&other_obj);

  TrustedPointerHandle code_handle =
      table.AllocateAndInitializeEntry(code_addr, kCodeIndirectPointerTag);
  TrustedPointerHandle trusted_handle = table.AllocateAndInitializeEntry(
      trusted_addr, kGenericTrustedObjectTag);

  // Exact-tag lookups succeed.
  assert(table.Get(code_handle, kCodeIndirectPointerTag) == code_addr);
  assert(table.Get(trusted_handle, kGenericTrustedObjectTag) == trusted_addr);

  // A type-confused access -- asking for Code but the handle actually holds
  // a generic trusted object -- is rejected with kNullAddress, not the real
  // pointer.
  assert(table.Get(trusted_handle, kCodeIndirectPointerTag) == 0);
  assert(table.Get(code_handle, kGenericTrustedObjectTag) == 0);

  // Null and out-of-range handles are rejected the same safe way.
  assert(table.Get(TrustedPointerTable::kNullHandle, kCodeIndirectPointerTag) == 0);
  assert(table.Get(static_cast<TrustedPointerHandle>(9999),
                    kCodeIndirectPointerTag) == 0);

  // A wide tag range (e.g. "any trusted object") accepts either tag.
  IndirectPointerTagRange any_trusted(kGenericTrustedObjectTag,
                                       kCodeIndirectPointerTag);
  assert(table.Get(code_handle, any_trusted) == code_addr);
  assert(table.Get(trusted_handle, any_trusted) == trusted_addr);

  // Freeing then reallocating reuses the slot.
  TrustedPointerHandle other_handle =
      table.AllocateAndInitializeEntry(other_addr, kGenericTrustedObjectTag);
  assert(table.Contains(other_handle));
  table.FreeEntry(other_handle);
  assert(!table.Contains(other_handle));
  size_t size_before_reuse = table.SizeForTesting();
  TrustedPointerHandle reused =
      table.AllocateAndInitializeEntry(code_addr, kCodeIndirectPointerTag);
  assert(table.SizeForTesting() == size_before_reuse);
  assert(reused == other_handle);
  assert(table.Get(reused, kCodeIndirectPointerTag) == code_addr);

  // Set() overwrites content in place without reallocating a handle.
  table.Set(trusted_handle, other_addr, kGenericTrustedObjectTag);
  assert(table.Get(trusted_handle, kGenericTrustedObjectTag) == other_addr);

  // Compact(): five entries, free a middle "hole" then a genuine trailing
  // run; only the trailing run actually shrinks the table.
  TrustedPointerTable ct;
  TrustedPointerHandle h0 = ct.AllocateAndInitializeEntry(code_addr, kCodeIndirectPointerTag);
  TrustedPointerHandle h1 = ct.AllocateAndInitializeEntry(trusted_addr, kGenericTrustedObjectTag);
  TrustedPointerHandle h2 = ct.AllocateAndInitializeEntry(other_addr, kGenericTrustedObjectTag);
  TrustedPointerHandle h3 = ct.AllocateAndInitializeEntry(code_addr, kCodeIndirectPointerTag);
  TrustedPointerHandle h4 = ct.AllocateAndInitializeEntry(trusted_addr, kGenericTrustedObjectTag);
  assert(ct.SizeForTesting() == 5);

  ct.FreeEntry(h1);
  ct.Compact();
  assert(ct.SizeForTesting() == 5);  // h1's hole isn't trailing
  assert(!ct.Contains(h1));

  ct.FreeEntry(h3);
  ct.FreeEntry(h4);
  ct.Compact();
  assert(ct.SizeForTesting() == 3);  // h0, h1's hole, h2 remain
  assert(ct.Get(h0, kCodeIndirectPointerTag) == code_addr);
  assert(ct.Get(h2, kGenericTrustedObjectTag) == other_addr);
  assert(!ct.Contains(h3));
  assert(!ct.Contains(h4));

  std::printf("trusted_pointer_table_test: OK\n");
  return 0;
}
