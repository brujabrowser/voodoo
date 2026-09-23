// Golden test for IndirectPointerTable (IPT): exact-tag and tag-range
// lookups succeed, a type-confused access returns a clean 0 (never the
// real GPA), freed entries are reused via the freelist, and Compact()
// trims only a genuine trailing run -- same shape as
// trusted_pointer_table_test.cc/external_pointer_table_test.cc, since IPT
// is built on the exact same ExternalEntityTable<Entry>/TaggedPayload
// mechanism, just with a guest-physical-address payload instead of a host
// one -- see indirect-pointer-table.h's file comment for why that's a
// genuinely new, non-redundant table rather than a rename of either.
#include <cassert>
#include <cstdio>

#include "src/sandbox/indirect-pointer-table.h"

using v8::internal::GuestPointerTag;
using v8::internal::GuestPointerTagRange;
using v8::internal::IndirectPointerHandle;
using v8::internal::IndirectPointerTable;
using v8::internal::kAnyGuestPointer;

int main() {
  IndirectPointerTable table;

  // Two distinct kinds of guest resource, two distinct tags -- e.g. what
  // ../WASMTurboSpace/src/hv/hv-partition.cc uses to distinguish "this
  // handle names the GDT table's GPA" from "this handle names an IDT
  // stub's GPA".
  constexpr auto kGdtTag = static_cast<GuestPointerTag>(1);
  constexpr auto kIdtStubTag = static_cast<GuestPointerTag>(2);

  constexpr uint64_t kGdtGpa = 0x4000;
  constexpr uint64_t kIdtStubGpa = 0x6000;
  constexpr uint64_t kOtherGpa = 0x9000;

  IndirectPointerHandle gdt_handle = table.AllocateAndInitializeEntry(kGdtGpa, kGdtTag);
  IndirectPointerHandle stub_handle = table.AllocateAndInitializeEntry(kIdtStubGpa, kIdtStubTag);

  // Exact-tag lookups succeed.
  assert(table.Get(gdt_handle, kGdtTag) == kGdtGpa);
  assert(table.Get(stub_handle, kIdtStubTag) == kIdtStubGpa);

  // A type-confused access -- asking for the GDT tag but the handle
  // actually holds an IDT stub GPA -- is rejected with 0, not the real
  // GPA.
  assert(table.Get(stub_handle, kGdtTag) == 0);
  assert(table.Get(gdt_handle, kIdtStubTag) == 0);

  // Null and out-of-range handles are rejected the same safe way.
  assert(table.Get(IndirectPointerTable::kNullHandle, kGdtTag) == 0);
  assert(table.Get(static_cast<IndirectPointerHandle>(9999), kGdtTag) == 0);

  // A wide tag range (e.g. "any guest resource") accepts either tag.
  assert(table.Get(gdt_handle, kAnyGuestPointer) == kGdtGpa);
  assert(table.Get(stub_handle, kAnyGuestPointer) == kIdtStubGpa);

  // Freeing then reallocating reuses the slot.
  IndirectPointerHandle other_handle = table.AllocateAndInitializeEntry(kOtherGpa, kGdtTag);
  assert(table.Contains(other_handle));
  table.FreeEntry(other_handle);
  assert(!table.Contains(other_handle));
  size_t size_before_reuse = table.SizeForTesting();
  IndirectPointerHandle reused = table.AllocateAndInitializeEntry(kGdtGpa, kGdtTag);
  assert(table.SizeForTesting() == size_before_reuse);
  assert(reused == other_handle);
  assert(table.Get(reused, kGdtTag) == kGdtGpa);

  // Set() overwrites content in place without reallocating a handle.
  table.Set(stub_handle, kOtherGpa, kIdtStubTag);
  assert(table.Get(stub_handle, kIdtStubTag) == kOtherGpa);

  // Compact(): five entries, free a middle "hole" then a genuine trailing
  // run; only the trailing run actually shrinks the table.
  IndirectPointerTable ct;
  IndirectPointerHandle h0 = ct.AllocateAndInitializeEntry(kGdtGpa, kGdtTag);
  IndirectPointerHandle h1 = ct.AllocateAndInitializeEntry(kIdtStubGpa, kIdtStubTag);
  IndirectPointerHandle h2 = ct.AllocateAndInitializeEntry(kOtherGpa, kGdtTag);
  IndirectPointerHandle h3 = ct.AllocateAndInitializeEntry(kGdtGpa, kGdtTag);
  IndirectPointerHandle h4 = ct.AllocateAndInitializeEntry(kIdtStubGpa, kIdtStubTag);
  assert(ct.SizeForTesting() == 5);

  ct.FreeEntry(h1);
  ct.Compact();
  assert(ct.SizeForTesting() == 5);  // h1's hole isn't trailing
  assert(!ct.Contains(h1));

  ct.FreeEntry(h3);
  ct.FreeEntry(h4);
  ct.Compact();
  assert(ct.SizeForTesting() == 3);  // h0, h1's hole, h2 remain
  assert(ct.Get(h0, kGdtTag) == kGdtGpa);
  assert(ct.Get(h2, kGdtTag) == kOtherGpa);
  assert(!ct.Contains(h3));
  assert(!ct.Contains(h4));

  std::printf("indirect_pointer_table_test: OK\n");
  return 0;
}
