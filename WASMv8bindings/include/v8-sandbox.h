// Adapted from V8's include/v8-sandbox.h. The TagRange<> template used to be
// folded in here directly (a standalone copy of V8's include/v8-internal.h
// TagRange, back when this port had no v8-internal.h of its own) -- now that
// WASMSafeSpace vendors a real v8-internal.h (Address, KB/MB/GB, and the
// same TagRange<Tag>, needed there for IndirectPointerTag/ExternalPointerTag
// too) and this repo links it as a sibling (see CMakeLists.txt's
// WV8_WSS_DIR), TagRange lives in exactly one place -- WASMSafeSpace's
// v8-internal.h -- included below, matching real V8's own file layout
// (TagRange is defined in v8-internal.h there too, not v8-sandbox.h).
//
// This is the *type* side of the CppHeapPointerTable mechanism: real V8
// wraps a `void*` into a JS-visible wrapper object (`v8::Object::Wrap()`)
// by storing a small `CppHeapPointerHandle` on the object and indirecting
// through a table (src/sandbox/cppheap-pointer-table.h) keyed by that
// handle, range-checking a `CppHeapPointerTag` on every access so a
// corrupted or wrong-typed handle can't be used to forge a pointer of the
// wrong C++ type. See that header's file comment for what's real vs.
// simplified about the table itself on this port -- it's now built directly
// on WASMSafeSpace's ExternalEntityTable<Entry>/TaggedPayload rather than a
// standalone freelist.
//
// The tag-range assignment algorithm described below (contiguous ranges via
// a post-order walk of a type hierarchy) is real and unmodified from
// upstream -- it's exactly the kind of thing brujac's resolver.cc already
// computes today for its "argument accepted from any descendant" Node*
// polymorphism check (see WASMBruja's README), so a future V8 codegen
// backend for brujac can reuse that same descendant-set computation to
// assign these ranges per Web IDL interface hierarchy.
#ifndef WASMV8_INCLUDE_V8_SANDBOX_H_
#define WASMV8_INCLUDE_V8_SANDBOX_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "v8-internal.h"
#include "v8config.h"

namespace v8 {

/**
 * A tag identifying the C++ type stored behind a CppHeapPointerHandle.
 *
 * Real V8 reserves a large block of tag values for its own internal
 * Oilpan-wrapped objects (kFirstV8InternalTag..kLastV8InternalTag) and
 * leaves a range below that for the embedder to assign per type hierarchy:
 * assign contiguous tag ids via a post-order walk of the type tree (leaves
 * first) so every supertype's range exactly covers its subtypes' ranges --
 * see the worked example in real V8's own include/v8-sandbox.h (vendored at
 * third_party/v8/include/... is not present since this file wasn't
 * originally vendored verbatim; the algorithm is reproduced in this file's
 * comment above).
 */
enum class CppHeapPointerTag : uint16_t {
  kNullTag = 0,
  kFirstObjectWrappableTag = 1,
  // Embedder-assignable range: [kFirstObjectWrappableTag, kLastObjectWrappableTag).
  kLastObjectWrappableTag = 0x7ffc,
  kZappedEntryTag = 0x7ffd,
  kEvacuationEntryTag = 0x7ffe,
  kFreeEntryTag = 0x7fff,
};

using CppHeapPointerTagRange = internal::TagRange<CppHeapPointerTag>;

constexpr CppHeapPointerTagRange kAnyCppHeapPointer(
    CppHeapPointerTag::kFirstObjectWrappableTag,
    CppHeapPointerTag::kLastObjectWrappableTag);

using CppHeapPointerHandle = uint32_t;
constexpr CppHeapPointerHandle kNullCppHeapPointerHandle = 0;

// Runtime-unique tag allocator for embedders that can't assign tags at
// compile time -- e.g. brujac's V8 codegen backend (WASMBruja's
// cpp_generator_v8.cc), which generates one independent header per .bruja
// file with no visibility into any other generated header's own interface
// set, so two separately-generated headers linked into the same program
// (WASMv16's console_gen.h + navigator_gen.h + ...) can't safely both start
// numbering their own tags at kFirstObjectWrappableTag. Mirrors quickjs-ng's
// own JS_NewClassID: allocate once, lazily, on first use, guarded by "have I
// already been assigned a tag" -- see cpp_generator_v8.cc's EnsureXTemplate
// for the exact pattern. Contiguous per-interface tag *ranges* (the
// post-order-walk scheme described on CppHeapPointerTag above) are still the
// right tool once a single generated header's interfaces have a real
// inheritance hierarchy to range over -- this allocator only solves the
// separate, single-tag-per-flat-interface, cross-header-uniqueness problem.
// Not thread-safe -- matches every other piece of shared mutable state in
// this single-threaded-JS-engine family (e.g. cpp_generator.cc's own
// g_X_class_id globals).
// Shared bump counter backing both allocators below -- a single-tag
// allocation is just a `count == 1` block, so both draw from the same
// pool and can never hand out overlapping values.
inline uint16_t& NextCppHeapPointerTagValue() {
  static uint16_t next = static_cast<uint16_t>(CppHeapPointerTag::kFirstObjectWrappableTag);
  return next;
}

inline CppHeapPointerTag AllocateCppHeapPointerTag() {
  uint16_t& next = NextCppHeapPointerTagValue();
  uint16_t value = next++;
  return static_cast<CppHeapPointerTag>(value);
}

// Allocates `count` contiguous tag values at once, returning the first --
// the actual per-interface tag-range tool the comment on CppHeapPointerTag
// above describes: an embedder (brujac's V8 backend, for one whole
// `interface`-inheritance forest in a single .bruja file) computes each
// interface's *local* pre-order index and subtree-max index once, at
// codegen time (real, unmodified V8 algorithm -- contiguous ids via a
// tree walk so every supertype's range covers its subtypes'), then adds
// this runtime-allocated base to each at first use. Splitting the
// allocation this way (one block call up front, cheap local arithmetic
// after) keeps the cross-header uniqueness problem AllocateCppHeapPointerTag
// solves and the per-hierarchy contiguous-range problem this solves fully
// independent of each other.
inline CppHeapPointerTag AllocateCppHeapPointerTagRange(uint16_t count) {
  uint16_t& next = NextCppHeapPointerTagValue();
  uint16_t base = next;
  next = static_cast<uint16_t>(next + count);
  return static_cast<CppHeapPointerTag>(base);
}

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_SANDBOX_H_
