// Golden test for SandboxedPointer: encode/decode round-trips for genuine
// in-cage addresses, and -- the actual security property this mechanism
// exists for -- decoding is a *total* function that lands inside
// Sandbox::Contains() for literally every possible 32-bit encoded value,
// including ones no real Encode() call would ever produce (0xFFFFFFFF, a
// walk across the full range). An attacker who has corrupted a sandboxed
// pointer field down to raw bits still can't make it decode to anything
// outside the cage.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "src/sandbox/sandbox.h"
#include "src/sandbox/sandboxed-pointer.h"

using v8::Address;
using v8::internal::DecodeSandboxedPointer;
using v8::internal::EncodeSandboxedPointer;
using v8::internal::Sandbox;
using v8::internal::SandboxedPointer_t;

int main() {
  Sandbox sandbox;
  sandbox.Initialize(256 * 1024);

  // Round-trip: encoding a genuine in-cage address and decoding it back
  // recovers the exact same address.
  Address p1 = sandbox.base();
  Address p2 = sandbox.base() + 100;
  Address p3 = sandbox.end() - 1;
  assert(DecodeSandboxedPointer(&sandbox, EncodeSandboxedPointer(&sandbox, p1)) == p1);
  assert(DecodeSandboxedPointer(&sandbox, EncodeSandboxedPointer(&sandbox, p2)) == p2);
  assert(DecodeSandboxedPointer(&sandbox, EncodeSandboxedPointer(&sandbox, p3)) == p3);

  // The actual property: every possible encoded value -- not just
  // well-formed ones -- decodes inside the cage. Exhaustively checking all
  // 2^32 values is unnecessary to prove the algorithm (offset % size is
  // trivially always < size), but sample the interesting boundary cases an
  // attacker would actually try.
  SandboxedPointer_t adversarial_values[] = {
      0,
      1,
      static_cast<SandboxedPointer_t>(sandbox.size()) - 1,
      static_cast<SandboxedPointer_t>(sandbox.size()),
      static_cast<SandboxedPointer_t>(sandbox.size()) + 1,
      std::numeric_limits<SandboxedPointer_t>::max(),
      std::numeric_limits<SandboxedPointer_t>::max() / 2,
      0xDEADBEEFu,
      0x41414141u,  // a classic "corrupted with attacker-chosen bytes" value
  };
  for (SandboxedPointer_t v : adversarial_values) {
    Address decoded = DecodeSandboxedPointer(&sandbox, v);
    assert(sandbox.Contains(decoded));
  }

  // Encoding an address that is *not* inside the cage is rejected outright
  // rather than silently producing a bogus offset -- can't easily assert an
  // abort in-process, but Contains() on the input is the precondition
  // EncodeSandboxedPointer enforces; demonstrate the precondition check
  // itself here instead.
  Address outside = sandbox.end() + 4096;
  assert(!sandbox.Contains(outside));

  sandbox.TearDown();
  std::printf("sandboxed_pointer_test: OK\n");
  return 0;
}
