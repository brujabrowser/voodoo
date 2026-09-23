// IndirectPointerTable (IPT): a new table in this family, not a strip-port
// of anything upstream -- V8 itself has no equivalent, because V8 never
// needs to reference *guest physical memory inside a hardware-virtualized
// partition*. TrustedPointerTable (trusted-pointer-table.h) and
// ExternalPointerTable (external-pointer-table.h) both indirect to *host*
// addresses (a host heap object, a raw host C++ pointer); WASMv8bindings'
// CppHeapPointerTable indirects to a *host* cppgc-managed payload. None of
// the three cover the one thing a WHv-backed virtual processor (see
// ../WASMTurboSpace/src/hv/hv-partition.h) actually needs: a safe way to
// reference a *guest physical address* (a GPA -- an offset into the small,
// explicitly-mapped memory region a partition's guest CPU can see) without
// scattering raw `constexpr uint64_t kXxxGpa = 0x1234;`-style magic
// constants through the code that builds a partition's GDT/IDT/stub
// layout. IPT closes that gap using the exact same mechanism TPT/EPT
// already use (ExternalEntityTable<Entry> + TaggedPayload, see both
// files' comments) -- same tag-checked-indirection discipline this whole
// family applies to every other class of sensitive reference, just with a
// payload type (a GPA, `uint64_t`) and tag catalog (GuestPointerTag) of
// its own.
//
// Real, independent bit layout (not copied from TPT's or EPT's, same
// "visibly independent instantiations of the same generic mechanism"
// reasoning external-pointer-table.h's own file comment gives for why its
// split differs from TPT's): 8-bit tag, 1-bit mark, 55-bit payload -- a
// GPA payload never needs anywhere close to 55 bits for any partition this
// family builds, but there's no reason to make it narrower than the two
// existing tables' payload fields just because the *use* happens to be
// small right now.
#ifndef WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TABLE_H_
#define WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TABLE_H_

#include <cstdint>
#include <optional>

#include "src/sandbox/external-entity-table.h"
#include "src/sandbox/tagged-payload.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

constexpr uint64_t kIndirectPointerTableTagShift = 56;
constexpr uint64_t kIndirectPointerTableMarkBit = 0x0080'0000'0000'0000ULL;
constexpr uint64_t kIndirectPointerTableTagMask = 0xff00'0000'0000'0000ULL;
constexpr uint64_t kIndirectPointerTablePayloadMask = 0x007f'ffff'ffff'ffffULL;
constexpr uint64_t kIndirectPointerTablePayloadShift = 0;

// What kind of guest-physical-address reference a given IPT entry names --
// this table's own tag catalog, independent of IndirectPointerTag (TPT)
// and ExternalPointerTag (EPT). An embedder widens this the same way it
// would widen either of those: add a tag, keep the reserved special tags
// at the same numeric positions (see indirect-pointer-tag.h's own file
// comment on that same convention).
//
// `: uint16_t`, not `uint8_t`, even though the packed word only actually
// spends 8 bits on the tag field (kIndirectPointerTableTagShift/Mask
// above) and nothing here needs more than a handful of distinct tags:
// TagRange<Tag> (v8-internal.h) hard-`static_assert`s its Tag parameter's
// underlying type is uint16_t, the same convention IndirectPointerTag and
// ExternalPointerTag already follow -- the packed-word tag *field* width
// and the C++ enum's own underlying-type width are independent choices.
enum class GuestPointerTag : uint16_t {
  kGuestPointerNullTag = 0,

  // Embedder-assignable range -- e.g. ../WASMTurboSpace/src/hv/
  // hv-partition.cc's GDT table, IDT table, per-vector IDT stubs, the
  // shared exception handler, and the fault-info reporting slot each get
  // their own tag here, so a GDT handle can never be mistaken for (or
  // resolve as if it were) an IDT stub handle even though both are "just"
  // GPAs.
  kFirstGuestResourceTag = 1,
  kLastGuestResourceTag = 0xfc,

  kGuestPointerZappedEntryTag = 0xfd,
  kGuestPointerEvacuationEntryTag = 0xfe,
  kGuestPointerFreeEntryTag = 0xff,
};

using GuestPointerTagRange = TagRange<GuestPointerTag>;
constexpr GuestPointerTagRange kAnyGuestPointer(GuestPointerTag::kFirstGuestResourceTag,
                                                 GuestPointerTag::kLastGuestResourceTag);

using IndirectPointerHandle = uint32_t;
constexpr IndirectPointerHandle kNullIndirectPointerHandle = 0;

struct IndirectPointerTableEntry {
  struct TaggingScheme {
    using TagType = GuestPointerTag;
    static constexpr uint64_t kMarkBit = kIndirectPointerTableMarkBit;
    static constexpr uint64_t kTagShift = kIndirectPointerTableTagShift;
    static constexpr uint64_t kTagMask = kIndirectPointerTableTagMask;
    static constexpr uint64_t kPayloadMask = kIndirectPointerTablePayloadMask;
    static constexpr uint64_t kPayloadShift = kIndirectPointerTablePayloadShift;
    static constexpr TagType kFreeEntryTag = GuestPointerTag::kGuestPointerFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        GuestPointerTag::kGuestPointerEvacuationEntryTag;
  };
  using Payload = TaggedPayload<TaggingScheme>;

  static IndirectPointerTableEntry MakeFreelistEntry(uint32_t next_free_index) {
    IndirectPointerTableEntry entry;
    entry.payload_ = Payload(next_free_index, GuestPointerTag::kGuestPointerFreeEntryTag);
    return entry;
  }

  bool ContainsFreelistLink() const { return payload_.ContainsFreelistLink(); }
  std::optional<uint32_t> ExtractFreelistLink() const {
    return payload_.ExtractFreelistLink();
  }

  Payload payload_{};
};

class IndirectPointerTable final : public ExternalEntityTable<IndirectPointerTableEntry> {
 public:
  static constexpr IndirectPointerHandle kNullHandle = kNullIndirectPointerHandle;

  IndirectPointerTable() = default;
  IndirectPointerTable(const IndirectPointerTable&) = delete;
  IndirectPointerTable& operator=(const IndirectPointerTable&) = delete;

  // Allocates a new entry naming guest physical address `gpa`, tagged
  // `tag`.
  IndirectPointerHandle AllocateAndInitializeEntry(uint64_t gpa, GuestPointerTag tag);

  // Returns the stored GPA if `handle` is valid and its tag falls within
  // `tag_range`; 0 otherwise -- a freed entry, an out-of-range handle, and
  // a tag mismatch are all indistinguishable to the caller, by design
  // (see tagged-payload.h), same as TPT/EPT's own Get().
  uint64_t Get(IndirectPointerHandle handle, GuestPointerTagRange tag_range) const;

  void Set(IndirectPointerHandle handle, uint64_t gpa, GuestPointerTag tag);

  // Returns the entry to the freelist. No-op on kNullHandle.
  void FreeEntry(IndirectPointerHandle handle);

  bool Contains(IndirectPointerHandle handle) const;

  // See external-entity-table.h's Compact() comment.
  void Compact() { ExternalEntityTable::Compact(); }

  using ExternalEntityTable::SizeForTesting;

 private:
  static uint32_t HandleToIndex(IndirectPointerHandle handle) { return handle - 1; }
  static IndirectPointerHandle IndexToHandle(uint32_t index) { return index + 1; }
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TABLE_H_
