// This port's ExternalPointerTable: real V8's EPT (src/sandbox/
// external-pointer-table.h, vendored at third_party/v8/src/sandbox/
// external-pointer-table.h) is how sandboxed heap objects safely reference
// raw C++ pointers that live entirely outside V8's own object model --
// typedArray/ArrayBuffer backing-store allocators, embedder-managed
// resources, anything that's just a `void*` handed to V8 from the outside.
// Structurally this is the same tag-range-checked-indirection mechanism as
// TrustedPointerTable (trusted-pointer-table.h) -- same ExternalEntityTable
// base, same TaggedPayload packing -- applied to a different tag universe
// (this port's own ExternalPointerTag, standing in for upstream's much
// larger real tag catalog tied to V8's specific embedder resource types
// (ArrayBufferExtension, Managed<T>, WaiterQueueNode, ...), which don't
// exist in this port and so aren't reproduced -- see
// indirect-pointer-tag.h's file comment for the same reasoning applied to
// IndirectPointerTag).
//
// Real EPT's bit layout is independently chosen from TPT's (different
// tag/mark/payload split -- see src/common/globals.h upstream, not
// vendored here). This port picks its own concrete split -- 16-bit tag,
// 1-bit mark, 47-bit payload -- rather than copying TPT's 15/1/48 split
// verbatim, specifically so the two tables are visibly independent
// instantiations of the same generic TaggedPayload<Scheme> mechanism
// (tagged-payload.h) rather than accidentally identical ones.
#ifndef WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_POINTER_TABLE_H_
#define WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_POINTER_TABLE_H_

#include <cstdint>
#include <optional>

#include "src/sandbox/external-entity-table.h"
#include "src/sandbox/tagged-payload.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

constexpr uint64_t kExternalPointerTagShift = 48;
constexpr uint64_t kExternalPointerMarkBit = 0x0000'8000'0000'0000ULL;
constexpr uint64_t kExternalPointerTagMask = 0xffff'0000'0000'0000ULL;
constexpr uint64_t kExternalPointerPayloadMask = 0x0000'7fff'ffff'ffffULL;
constexpr uint64_t kExternalPointerPayloadShift = 0;

enum class ExternalPointerTag : uint16_t {
  kExternalPointerNullTag = 0,

  kFirstManagedResourceTag = 1,
  // Embedder-assignable range, same post-order-per-hierarchy convention as
  // CppHeapPointerTag (see ../WASMv8bindings/include/v8-sandbox.h).
  kLastManagedResourceTag = 0x7ffc,

  kExternalPointerZappedEntryTag = 0x7ffd,
  kExternalPointerEvacuationEntryTag = 0x7ffe,
  kExternalPointerFreeEntryTag = 0x7fff,
};

using ExternalPointerTagRange = TagRange<ExternalPointerTag>;
constexpr ExternalPointerTagRange kAnyExternalPointer(
    ExternalPointerTag::kFirstManagedResourceTag,
    ExternalPointerTag::kLastManagedResourceTag);

using ExternalPointerHandle = uint32_t;
constexpr ExternalPointerHandle kNullExternalPointerHandle = 0;

struct ExternalPointerTableEntry {
  struct TaggingScheme {
    using TagType = ExternalPointerTag;
    static constexpr uint64_t kMarkBit = kExternalPointerMarkBit;
    static constexpr uint64_t kTagShift = kExternalPointerTagShift;
    static constexpr uint64_t kTagMask = kExternalPointerTagMask;
    static constexpr uint64_t kPayloadMask = kExternalPointerPayloadMask;
    static constexpr uint64_t kPayloadShift = kExternalPointerPayloadShift;
    static constexpr TagType kFreeEntryTag =
        ExternalPointerTag::kExternalPointerFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        ExternalPointerTag::kExternalPointerEvacuationEntryTag;
  };
  using Payload = TaggedPayload<TaggingScheme>;

  static ExternalPointerTableEntry MakeFreelistEntry(uint32_t next_free_index) {
    ExternalPointerTableEntry entry;
    entry.payload_ =
        Payload(next_free_index, ExternalPointerTag::kExternalPointerFreeEntryTag);
    return entry;
  }

  bool ContainsFreelistLink() const { return payload_.ContainsFreelistLink(); }
  std::optional<uint32_t> ExtractFreelistLink() const {
    return payload_.ExtractFreelistLink();
  }

  Payload payload_{};
};

class ExternalPointerTable final
    : public ExternalEntityTable<ExternalPointerTableEntry> {
 public:
  static constexpr ExternalPointerHandle kNullHandle = kNullExternalPointerHandle;

  ExternalPointerTable() = default;
  ExternalPointerTable(const ExternalPointerTable&) = delete;
  ExternalPointerTable& operator=(const ExternalPointerTable&) = delete;

  ExternalPointerHandle AllocateAndInitializeEntry(void* pointer,
                                                    ExternalPointerTag tag);

  void* Get(ExternalPointerHandle handle, ExternalPointerTagRange tag_range) const;
  void Set(ExternalPointerHandle handle, void* pointer, ExternalPointerTag tag);
  void FreeEntry(ExternalPointerHandle handle);
  bool Contains(ExternalPointerHandle handle) const;
  void Compact() { ExternalEntityTable::Compact(); }

  using ExternalEntityTable::SizeForTesting;

 private:
  static uint32_t HandleToIndex(ExternalPointerHandle handle) { return handle - 1; }
  static ExternalPointerHandle IndexToHandle(uint32_t index) { return index + 1; }
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_EXTERNAL_POINTER_TABLE_H_
