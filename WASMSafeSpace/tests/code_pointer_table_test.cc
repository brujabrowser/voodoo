// Golden test for CodePointerTable: an allocated entry's entrypoint and
// code-object pointer round-trip correctly, SetEntrypoint can retarget an
// entry in place (the shape a real tiering compiler swapping in optimized
// code needs), a corrupted/out-of-range handle safely yields kNullAddress
// rather than an arbitrary address, and freed entries are reused via the
// freelist. See code-pointer-table.h's file comment for the real,
// documented scope of the control-flow-integrity property this table
// provides (a forged handle can only ever select *some* legitimately
// registered entrypoint, never injected/arbitrary memory).
#include <cassert>
#include <cstdio>

#include "src/sandbox/code-pointer-table.h"

using v8::Address;
using v8::internal::CodePointerHandle;
using v8::internal::CodePointerTable;

namespace {
int FunctionA(int x) { return x + 1; }
int FunctionB(int x) { return x * 2; }
using FnPtr = int (*)(int);
}  // namespace

int main() {
  CodePointerTable table;

  int fake_code_object_a = 0xA;
  int fake_code_object_b = 0xB;
  Address code_a = reinterpret_cast<Address>(&fake_code_object_a);
  Address code_b = reinterpret_cast<Address>(&fake_code_object_b);
  Address entry_a = reinterpret_cast<Address>(&FunctionA);
  Address entry_b = reinterpret_cast<Address>(&FunctionB);

  CodePointerHandle handle_a = table.AllocateAndInitializeEntry(code_a, entry_a);
  CodePointerHandle handle_b = table.AllocateAndInitializeEntry(code_b, entry_b);

  assert(table.GetEntrypoint(handle_a) == entry_a);
  assert(table.GetCodeObject(handle_a) == code_a);
  assert(table.GetEntrypoint(handle_b) == entry_b);
  assert(table.GetCodeObject(handle_b) == code_b);

  // Real indirect dispatch through the table -- proves this isn't just
  // bookkeeping: the entrypoint recovered from the table is genuinely
  // callable and calls the right function.
  FnPtr fn_a = reinterpret_cast<FnPtr>(table.GetEntrypoint(handle_a));
  FnPtr fn_b = reinterpret_cast<FnPtr>(table.GetEntrypoint(handle_b));
  assert(fn_a(41) == 42);
  assert(fn_b(21) == 42);

  // Null and out-of-range handles are rejected safely.
  assert(table.GetEntrypoint(CodePointerTable::kNullHandle) == 0);
  assert(table.GetEntrypoint(static_cast<CodePointerHandle>(9999)) == 0);

  // A tiering compiler retargeting an entry in place: the handle stays the
  // same, every existing holder of it now dispatches to the new code.
  table.SetEntrypoint(handle_a, entry_b);
  FnPtr fn_a_retargeted = reinterpret_cast<FnPtr>(table.GetEntrypoint(handle_a));
  assert(fn_a_retargeted(21) == 42);  // now runs FunctionB's logic
  assert(table.GetCodeObject(handle_a) == code_a);  // code object untouched

  // Freeing then reallocating reuses the slot.
  assert(table.Contains(handle_b));
  table.FreeEntry(handle_b);
  assert(!table.Contains(handle_b));
  size_t size_before_reuse = table.SizeForTesting();
  CodePointerHandle reused = table.AllocateAndInitializeEntry(code_b, entry_a);
  assert(table.SizeForTesting() == size_before_reuse);
  assert(reused == handle_b);
  assert(table.GetEntrypoint(reused) == entry_a);

  std::printf("code_pointer_table_test: OK\n");
  return 0;
}
