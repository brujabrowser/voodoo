// This port's SandboxedPointer: real V8's sandboxed pointers
// (src/sandbox/sandboxed-pointer.h/-inl.h, vendored at third_party/v8/src/
// sandbox/) are how objects *inside* the cage reference each other without
// using raw pointers: a sandboxed pointer field stores an *offset* from the
// cage's base address rather than an absolute address, so that even if an
// attacker fully controls the stored bits, decoding them can only ever
// produce an address of the form `base + offset` -- somewhere inside the
// cage (or, in real V8's case, inside the surrounding guard regions, which
// are unmapped and so still safe to have produced, just not dereferenceable
// garbage outside the reservation).
//
// Real V8's encode/decode (`WriteSandboxedPointerField`/
// `ReadSandboxedPointerField`) is exactly `offset = pointer - base` /
// `pointer = base + offset`, plus a left/right shift
// (`kSandboxedPointerShift`) that packs the offset into fewer bits than a
// full pointer would need, relying on the ~1TB cage's huge surrounding
// guard regions to absorb any offset value that doesn't correspond to a
// real live object. This port has no guard regions to absorb an
// out-of-range offset (see sandbox.h's file comment), so instead of a
// shift this port's Encode/Decode explicitly wrap the offset modulo the
// cage's actual size -- a *stronger* containment guarantee than upstream's
// shift-only scheme for this port's specific (guard-region-less) setup:
// `DecodeSandboxedPointer` is proven by sandboxed_pointer_test.cc to return
// an address satisfying `Sandbox::Contains()` for *every* possible 32-bit
// input, not just well-formed ones -- the actual real-world property a
// sandboxed pointer is supposed to guarantee, made unconditionally true
// here rather than "true as long as the guard regions are big enough."
#ifndef WASMSAFESPACE_SRC_SANDBOX_SANDBOXED_POINTER_H_
#define WASMSAFESPACE_SRC_SANDBOX_SANDBOXED_POINTER_H_

#include <cstdint>

#include "src/sandbox/sandbox.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

// The compressed, in-cage-relative representation. 32 bits: this port's
// 64MB default cage (sandbox.h) needs at most 26 significant bits, so a
// 32-bit field is already generous headroom, not a squeeze -- unlike real
// V8, which compresses a ~40-bit offset (1TB cage) into a narrower field
// for memory-density reasons.
using SandboxedPointer_t = uint32_t;

// Decodes `encoded` against `sandbox`. Total function: every possible
// `SandboxedPointer_t` value decodes to *some* address inside
// `sandbox->Contains()` -- see file comment.
V8_INLINE Address DecodeSandboxedPointer(const Sandbox* sandbox,
                                          SandboxedPointer_t encoded) {
  DCHECK(sandbox->is_initialized());
  uint64_t offset = static_cast<uint64_t>(encoded) % sandbox->size();
  return sandbox->base() + static_cast<Address>(offset);
}

// Encodes `pointer`, which must already satisfy `sandbox->Contains(pointer)`
// (real V8 CHECKs this too -- see WriteSandboxedPointerField).
V8_INLINE SandboxedPointer_t EncodeSandboxedPointer(const Sandbox* sandbox,
                                                     Address pointer) {
  CHECK(sandbox->Contains(pointer));
  uint64_t offset =
      static_cast<uint64_t>(pointer) - static_cast<uint64_t>(sandbox->base());
  return static_cast<SandboxedPointer_t>(offset);
}

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_SANDBOXED_POINTER_H_
