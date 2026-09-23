// Trimmed strip-port of real V8's src/sandbox/indirect-pointer-tag.h
// (vendored at third_party/v8/src/sandbox/indirect-pointer-tag.h). The bit
// layout constants (kTrustedPointerTableTagMask/MarkBit/PayloadMask/
// TagShift/PayloadShift) and the fast-tag helpers
// (IsFastIndirectPointerTag/IsFastIndirectPointerTagRange/
// ComputeUntaggingMaskForFastIndirectPointerTag) are copied verbatim --
// pure bit arithmetic, no OS/object-model dependency, so nothing to
// simplify.
//
// What's cut: the ~15 V8-internal tags (WasmDispatchTable,
// BytecodeArray, InterpreterData, RegExpData, UncompiledData, ...) and
// `IndirectPointerTagFromInstanceType()`, which switches on real V8's
// `InstanceType` enum -- an artifact of V8's actual object model that this
// port has no equivalent of. In its place: a small, self-contained tag set
// covering exactly what this port's own tests/examples need (a generic
// trusted-object tag, a code tag, and the required null/free/zapped/
// unpublished special tags upstream itself reserves at the same numeric
// positions). An embedder integrating this port for real would extend this
// enum with its own object-type tags the same way upstream's is extended
// with V8's -- the mechanism (tag-range checked indirection) is what's
// real here, not the specific tag catalog.
#ifndef WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TAG_H_
#define WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TAG_H_

#include <cstdint>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

// A trusted pointer table entry has the following layout, verbatim from
// upstream:
//
// +------------+----------+-----------------+
// | 15-bit tag | mark bit | 48-bit payload  |
// +------------+----------+-----------------+
constexpr uint64_t kTrustedPointerTableTagMask = 0xfffe'0000'0000'0000ULL;
constexpr uint64_t kTrustedPointerTableMarkBit = 0x0001'0000'0000'0000ULL;
constexpr uint64_t kTrustedPointerTablePayloadMask = 0x0000'ffff'ffff'ffffULL;
constexpr uint64_t kTrustedPointerTableTagShift = 49;
constexpr uint64_t kTrustedPointerTablePayloadShift = 0;

enum IndirectPointerTag : uint16_t {
  kIndirectPointerNullTag = 0,

  // Fast (power-of-two) tags: untaggable with a single AND, see
  // IsFastIndirectPointerTag below. This port's stand-in for upstream's
  // per-isolate trusted-object catalog.
  kGenericTrustedObjectTag = 1,
  kCodeIndirectPointerTag = 2,
  kLastIndirectPointerFastTag = 0x0f,

  // Special tags, at the same numeric positions upstream reserves them.
  kUnpublishedIndirectPointerTag = 0xfc,
  kIndirectPointerZappedEntryTag = 0xfd,
  kIndirectPointerEvacuationEntryTag = 0xfe,
  kIndirectPointerFreeEntryTag = 0xff,
  kLastIndirectPointerTag = 0xff,
};

using IndirectPointerTagRange = TagRange<IndirectPointerTag>;

// "Fast" tags are those that are powers of two: untagging is then a single
// mask instead of extract-and-compare. Verbatim from upstream.
V8_INLINE constexpr bool IsFastIndirectPointerTag(IndirectPointerTag tag) {
  DCHECK_NE(tag, kIndirectPointerNullTag);
  return base::bits::IsPowerOfTwo(static_cast<uint32_t>(tag));
}

constexpr IndirectPointerTagRange kAllIndirectPointerTags(
    kGenericTrustedObjectTag, kLastIndirectPointerFastTag);
constexpr IndirectPointerTagRange kAllIndirectPointerTagsIncludingUnpublished(
    kGenericTrustedObjectTag, kUnpublishedIndirectPointerTag);

V8_INLINE constexpr bool IsValidIndirectPointerTag(IndirectPointerTag tag) {
  return kAllIndirectPointerTags.Contains(tag);
}

static_assert(!IsValidIndirectPointerTag(kIndirectPointerNullTag));
static_assert(IsFastIndirectPointerTag(kCodeIndirectPointerTag));

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_INDIRECT_POINTER_TAG_H_
