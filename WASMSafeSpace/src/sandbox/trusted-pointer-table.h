// This port's TrustedPointerTable: real V8's TPT (src/sandbox/
// trusted-pointer-table.h, vendored at third_party/v8/src/sandbox/
// trusted-pointer-table.h) lets sandboxed heap objects safely reference
// "trusted" objects that live outside the sandbox -- accessed only through
// a tag-range-checked handle, so a corrupted or wrong-typed handle can
// never be used to forge a pointer to an object of the wrong type or to
// arbitrary memory. Built on this port's own ExternalEntityTable<Entry>
// base (external-entity-table.h) and the near-verbatim TaggedPayload
// (tagged-payload.h) -- see both files' comments for what's real vs.
// simplified about the underlying storage. The tag-check itself, and the
// {tag, mark bit, payload} single-word packing, are the real upstream
// mechanism and real upstream bit layout (kTrustedPointerTable* constants
// in indirect-pointer-tag.h), unmodified.
#ifndef WASMSAFESPACE_SRC_SANDBOX_TRUSTED_POINTER_TABLE_H_
#define WASMSAFESPACE_SRC_SANDBOX_TRUSTED_POINTER_TABLE_H_

#include <cstdint>
#include <optional>

#include "src/sandbox/external-entity-table.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/tagged-payload.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

using TrustedPointerHandle = uint32_t;
constexpr TrustedPointerHandle kNullTrustedPointerHandle = 0;

struct TrustedPointerTableEntry {
  struct TaggingScheme {
    using TagType = IndirectPointerTag;
    static constexpr uint64_t kMarkBit = kTrustedPointerTableMarkBit;
    static constexpr uint64_t kTagShift = kTrustedPointerTableTagShift;
    static constexpr uint64_t kTagMask = kTrustedPointerTableTagMask;
    static constexpr uint64_t kPayloadMask = kTrustedPointerTablePayloadMask;
    static constexpr uint64_t kPayloadShift = kTrustedPointerTablePayloadShift;
    static constexpr TagType kFreeEntryTag = kIndirectPointerFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        kIndirectPointerEvacuationEntryTag;
  };
  using Payload = TaggedPayload<TaggingScheme>;

  static TrustedPointerTableEntry MakeFreelistEntry(uint32_t next_free_index) {
    TrustedPointerTableEntry entry;
    entry.payload_ = Payload(next_free_index, kIndirectPointerFreeEntryTag);
    return entry;
  }

  bool ContainsFreelistLink() const { return payload_.ContainsFreelistLink(); }
  std::optional<uint32_t> ExtractFreelistLink() const {
    return payload_.ExtractFreelistLink();
  }

  Payload payload_{};
};

class TrustedPointerTable final : public ExternalEntityTable<TrustedPointerTableEntry> {
 public:
  static constexpr TrustedPointerHandle kNullHandle = kNullTrustedPointerHandle;

  TrustedPointerTable() = default;
  TrustedPointerTable(const TrustedPointerTable&) = delete;
  TrustedPointerTable& operator=(const TrustedPointerTable&) = delete;

  // Allocates a new entry containing `pointer` tagged with `tag`.
  TrustedPointerHandle AllocateAndInitializeEntry(Address pointer,
                                                   IndirectPointerTag tag);

  // Returns the stored pointer if `handle` is valid and its tag falls
  // within `tag_range`; kNullAddress otherwise -- a freed entry, an
  // out-of-range handle, and a tag mismatch are all indistinguishable to
  // the caller, by design (see tagged-payload.h).
  Address Get(TrustedPointerHandle handle, IndirectPointerTagRange tag_range) const;

  void Set(TrustedPointerHandle handle, Address pointer, IndirectPointerTag tag);

  // Returns the entry to the freelist. No-op on kNullHandle.
  void FreeEntry(TrustedPointerHandle handle);

  bool Contains(TrustedPointerHandle handle) const;

  // See external-entity-table.h's Compact() comment.
  void Compact() { ExternalEntityTable::Compact(); }

  using ExternalEntityTable::SizeForTesting;

 private:
  static uint32_t HandleToIndex(TrustedPointerHandle handle) { return handle - 1; }
  static TrustedPointerHandle IndexToHandle(uint32_t index) { return index + 1; }
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_TRUSTED_POINTER_TABLE_H_
