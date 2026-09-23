// Minimal usage example: reserve a cage, allocate inside it, wrap a pointer
// to something outside it in a tag-checked handle, and show that a
// corrupted pointer field can't escape the cage. The shape of real cppgc's
// own samples/cppgc/hello-world.cc, applied to this repo's Sandbox instead.
#include <cstdio>

#include "src/sandbox/sandbox.h"
#include "src/sandbox/sandboxed-pointer.h"
#include "src/sandbox/trusted-pointer-table.h"

using namespace v8;
using namespace v8::internal;

int main() {
  Sandbox sandbox;
  sandbox.Initialize();  // default 64MB cage
  Sandbox::set_current(&sandbox);
  std::printf("cage reserved: [0x%llx, 0x%llx), %zu bytes\n",
              static_cast<unsigned long long>(sandbox.base()),
              static_cast<unsigned long long>(sandbox.end()), sandbox.size());

  int* in_cage = static_cast<int*>(sandbox.Allocate(sizeof(int)));
  *in_cage = 42;
  SandboxedPointer_t field =
      EncodeSandboxedPointer(&sandbox, reinterpret_cast<Address>(in_cage));
  std::printf("stored 42 at cage offset %u\n", field);

  Address recovered = DecodeSandboxedPointer(&sandbox, field);
  std::printf("decoded back: %d\n", *reinterpret_cast<int*>(recovered));

  int outside = 7;  // deliberately NOT sandbox.Allocate()'d
  TrustedPointerTable table;
  TrustedPointerHandle h = table.AllocateAndInitializeEntry(
      reinterpret_cast<Address>(&outside), kGenericTrustedObjectTag);
  Address got = table.Get(h, kGenericTrustedObjectTag);
  std::printf("trusted object via handle: %d\n", *reinterpret_cast<int*>(got));

  Address rejected = table.Get(h, kCodeIndirectPointerTag);
  std::printf("wrong-tag access rejected: %s\n",
              rejected == kNullAddress ? "yes (nullptr)" : "NO -- BUG");

  sandbox.TearDown();
  return 0;
}
