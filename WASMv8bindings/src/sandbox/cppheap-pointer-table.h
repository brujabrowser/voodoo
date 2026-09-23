// This port's CppHeapPointerTable: the indirect-handle mechanism real V8
// uses to let a JS-visible wrapper object (v8::Object::Wrap()) reference a
// cppgc-managed C++ object safely -- a small CppHeapPointerHandle stored on
// the JS side, indirected through this table with a tag-range check on
// every access, so a corrupted/wrong-typed handle can't be used to forge a
// pointer of the wrong C++ type. See include/v8-sandbox.h for
// CppHeapPointerTag/CppHeapPointerTagRange.
//
// Real V8's CppHeapPointerTable (third_party/v8/src/sandbox/
// cppheap-pointer-table.*, vendored for reference) is a
// CompactibleExternalEntityTable: entries live in fixed-size segments
// carved out of one large OS-reserved region, support table-level
// compaction (SweepAndCompact, moving live entries down to free whole
// segments), and pack {pointer, tag, mark bit} into one atomic 64-bit word
// so a concurrent GC thread can mark/relocate entries without a lock.
//
// This table is now built on ../../WASMSafeSpace's
// v8::internal::ExternalEntityTable<Entry> (src/sandbox/
// external-entity-table.h) -- the same shared base real V8's own
// TrustedPointerTable/CodePointerTable/ExternalPointerTable/
// CppHeapPointerTable all derive from -- instead of a standalone
// std::vector<Entry>+freelist. Composing with that repo's Sandbox is what
// this repo's cppgc::internal::NormalPage now does too (see
// src/heap/internal/normal-page.h): a wrapped object's pointer, stored
// here, now genuinely lives inside the cage, while this table's own
// storage (inherited from ExternalEntityTable, a plain std::vector) lives
// outside it -- exactly real V8's actual shape. The entry layout follows
// TrustedPointerTableEntry's exact pattern (see
// ../../WASMSafeSpace/src/sandbox/trusted-pointer-table.h): a
// TaggedPayload<TaggingScheme> packs {tag, mark bit, payload} into one
// 64-bit word using the same bit widths as
// v8::internal::kTrustedPointerTable* (CppHeapPointerTag is also a
// uint16_t enum bounded by kFreeEntryTag = 0x7fff, i.e. 15 significant
// bits, so the same 15-bit-tag/1-mark-bit/48-bit-payload layout applies
// unchanged).
//
// The real security property -- a handle can only ever recover a pointer
// whose tag falls in the range the caller checked for -- is fully
// preserved: Get() returns nullptr on any tag mismatch or out-of-range
// handle rather than V8's own "return a poisoned pointer that crashes on
// dereference" trick, which relies on a real OS page fault handler wasm32
// linear memory doesn't have an equivalent of.
//
// Entry lifetime here is still caller-managed (FreeEntry must be called
// explicitly, and is -- see isolate.cc's quickjs wrapper finalizer, which
// calls it exactly when a JS wrapper object is collected) rather than
// reclaimed by a table-level mark pass -- there is no SweepAndCompact scan.
// Compact() (inherited from ExternalEntityTable, re-exposed public here) is
// this port's version of that property (not the mechanism -- there's no
// relocation of live entries to free whole segments, just trimming trailing
// freed entries off the vector, which needs no mark phase since liveness
// here is always exactly known from the freelist already).
#ifndef WASMV8_SRC_SANDBOX_CPPHEAP_POINTER_TABLE_H_
#define WASMV8_SRC_SANDBOX_CPPHEAP_POINTER_TABLE_H_

#include <cstdint>
#include <optional>

#include "src/sandbox/external-entity-table.h"
#include "src/sandbox/tagged-payload.h"
#include "v8-sandbox.h"

namespace cppgc {
namespace internal {

// Same bit layout as v8::internal::kTrustedPointerTable* (see
// ../../WASMSafeSpace/src/sandbox/indirect-pointer-tag.h): 15-bit tag |
// 1 mark bit | 48-bit payload, packed into a 64-bit word.
// CppHeapPointerTag (include/v8-sandbox.h) is bounded by kFreeEntryTag =
// 0x7fff, i.e. also exactly 15 significant bits, so this port reuses the
// same widths independently rather than sharing constants across the two
// (different) tag enums.
constexpr uint64_t kCppHeapPointerTableTagMask = 0xfffe'0000'0000'0000ULL;
constexpr uint64_t kCppHeapPointerTableMarkBit = 0x0001'0000'0000'0000ULL;
constexpr uint64_t kCppHeapPointerTablePayloadMask = 0x0000'ffff'ffff'ffffULL;
constexpr uint64_t kCppHeapPointerTableTagShift = 49;
constexpr uint64_t kCppHeapPointerTablePayloadShift = 0;

struct CppHeapPointerTableEntry {
  struct TaggingScheme {
    using TagType = v8::CppHeapPointerTag;
    static constexpr uint64_t kMarkBit = kCppHeapPointerTableMarkBit;
    static constexpr uint64_t kTagShift = kCppHeapPointerTableTagShift;
    static constexpr uint64_t kTagMask = kCppHeapPointerTableTagMask;
    static constexpr uint64_t kPayloadMask = kCppHeapPointerTablePayloadMask;
    static constexpr uint64_t kPayloadShift = kCppHeapPointerTablePayloadShift;
    static constexpr TagType kFreeEntryTag = v8::CppHeapPointerTag::kFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        v8::CppHeapPointerTag::kEvacuationEntryTag;
  };
  using Payload = v8::internal::TaggedPayload<TaggingScheme>;

  static CppHeapPointerTableEntry MakeFreelistEntry(uint32_t next_free_index) {
    CppHeapPointerTableEntry entry;
    entry.payload_ = Payload(next_free_index, v8::CppHeapPointerTag::kFreeEntryTag);
    return entry;
  }

  bool ContainsFreelistLink() const { return payload_.ContainsFreelistLink(); }
  std::optional<uint32_t> ExtractFreelistLink() const {
    return payload_.ExtractFreelistLink();
  }

  Payload payload_{};
};

class CppHeapPointerTable final
    : public v8::internal::ExternalEntityTable<CppHeapPointerTableEntry> {
 public:
  static constexpr v8::CppHeapPointerHandle kNullHandle =
      v8::kNullCppHeapPointerHandle;

  CppHeapPointerTable() = default;
  CppHeapPointerTable(const CppHeapPointerTable&) = delete;
  CppHeapPointerTable& operator=(const CppHeapPointerTable&) = delete;

  // Allocates a new entry (reusing a freed slot if one is available) and
  // stores `pointer` tagged with `tag`.
  v8::CppHeapPointerHandle AllocateAndInitializeEntry(void* pointer,
                                                      v8::CppHeapPointerTag tag);

  // Returns the stored pointer if `handle` is valid and its tag falls
  // within `tag_range`; nullptr otherwise (null handle, out-of-range
  // index, freed entry, or tag mismatch -- all indistinguishable to the
  // caller, by design: a type-confused caller gets a clean null, not a
  // pointer it can misuse).
  void* Get(v8::CppHeapPointerHandle handle,
           v8::CppHeapPointerTagRange tag_range) const;

  // Overwrites an existing entry's pointer and tag. `handle` must be a
  // handle previously returned by AllocateAndInitializeEntry and not yet
  // freed.
  void Set(v8::CppHeapPointerHandle handle, void* pointer,
          v8::CppHeapPointerTag tag);

  // Returns the entry to the freelist for reuse. A no-op on kNullHandle.
  void FreeEntry(v8::CppHeapPointerHandle handle);

  bool Contains(v8::CppHeapPointerHandle handle) const;

  // Trims trailing freed entries off the backing vector, actually
  // shrinking it rather than just marking capacity reusable -- see
  // ExternalEntityTable::Compact()'s comment. Not called automatically (no
  // GC-scheduling policy exists to decide when); a caller (or a future
  // integration with cppgc::Heap's own sweep) invokes it when it judges the
  // moment worthwhile. Existing handles into entries that remain are
  // unaffected; handles to trimmed entries were already invalid (freed)
  // before this call.
  void Compact() { ExternalEntityTable::Compact(); }

  using ExternalEntityTable::SizeForTesting;

 private:
  static uint32_t HandleToIndex(v8::CppHeapPointerHandle handle) {
    return handle - 1;
  }
  static v8::CppHeapPointerHandle IndexToHandle(uint32_t index) {
    return index + 1;
  }
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_SANDBOX_CPPHEAP_POINTER_TABLE_H_
