// Near-verbatim strip-port of real V8's src/sandbox/tagged-payload.h
// (vendored at third_party/v8/src/sandbox/tagged-payload.h), which is
// itself real V8's *whole* answer to "how do you pack a pointer, a type
// tag, and a GC marking bit into a single word": (1) is pure bit
// arithmetic with no OS/threading dependency, so unlike most of this repo
// it needed essentially no simplification to port -- the templated
// `PayloadTaggingScheme` parameter (kTagShift/kPayloadShift/kMarkBit/
// kPayloadMask/kTagMask/kFreeEntryTag/kEvacuationEntryTag) is exactly what
// lets TrustedPointerTable and ExternalPointerTable below share this one
// class with two completely different bit layouts, exactly like upstream.
//
// Two real differences from upstream, both explained where they matter:
//  - `encoded_word_` is always a plain `uint64_t` here, not `Address`
//    (`uintptr_t`). Real V8 relies on a 64-bit host `Address` leaving spare
//    high bits above a pointer's ~48 significant bits for the tag+mark to
//    live in; on this port's wasm32-wasip1 target `Address` is only 32
//    bits wide, with zero spare bits. Since the packed word is table
//    *storage*, not a value ever treated as a real pointer, there's no
//    reason it has to match pointer width -- widening it to a fixed
//    uint64_t keeps the exact same real packing algorithm working on a
//    32-bit target instead of forcing a redesign. Every concrete tagging
//    scheme below therefore declares its own {tag,mark,payload} bit layout
//    against this fixed 64-bit word, independent of target pointer width.
//  - `Untag()`'s dispatch on `ExternalPointerCanBeEmpty(tag_range)` (a
//    free function hook upstream defines per-tag-type, always returning a
//    fixed bool per real V8's own TODO-flagged workaround) is dropped;
//    callers here call `UntagAllowNullHandle`/`UntagDisallowNullHandle`
//    directly instead of through that indirection, since this port has no
//    use for compile-time-selected per-call-site null policy beyond what
//    each table's own `Get()` already decides once for all its entries.
#ifndef WASMSAFESPACE_SRC_SANDBOX_TAGGED_PAYLOAD_H_
#define WASMSAFESPACE_SRC_SANDBOX_TAGGED_PAYLOAD_H_

#include <cstdint>
#include <optional>

#include "v8-internal.h"

namespace v8 {
namespace internal {

// A generic payload struct used for the entries of a pointer table. It
// encodes (1) the pointer, (2) the type tag, and (3) the marking bit. The
// exact layout within the 64-bit word is determined by the
// `PayloadTaggingScheme` template parameter.
template <typename PayloadTaggingScheme>
struct TaggedPayload {
  using TagType = typename PayloadTaggingScheme::TagType;
  using TagRangeType = TagRange<TagType>;

  static constexpr uint64_t kTagShift = PayloadTaggingScheme::kTagShift;
  static constexpr uint64_t kPayloadShift = PayloadTaggingScheme::kPayloadShift;
  static constexpr uint64_t kPayloadMask = PayloadTaggingScheme::kPayloadMask;
  static constexpr uint64_t kTagMask = PayloadTaggingScheme::kTagMask;
  static constexpr uint64_t kMarkBit = PayloadTaggingScheme::kMarkBit;
  static constexpr TagType kFreeEntryTag = PayloadTaggingScheme::kFreeEntryTag;
  static constexpr TagType kEvacuationEntryTag =
      PayloadTaggingScheme::kEvacuationEntryTag;

  constexpr TaggedPayload() : encoded_word_(0) {}
  TaggedPayload(uint64_t pointer, TagType tag)
      : encoded_word_(Tag(pointer, tag)) {}

  static uint64_t Tag(uint64_t pointer, TagType tag) {
    return (pointer << kPayloadShift) |
           (static_cast<uint64_t>(tag) << kTagShift);
  }

  static bool CheckTag(uint64_t content, TagRangeType tag_range) {
    TagType tag = static_cast<TagType>((content & kTagMask) >> kTagShift);
    return tag_range.Contains(tag);
  }

  // Returns the payload if the tag matches `tag_range`, or 0 (a guaranteed-
  // invalid/null payload) on any mismatch -- used by tables whose "not
  // found" outcome should look identical to "found a null entry" rather
  // than being distinguishable to the caller (matches real V8's stated
  // reasoning: fewer branches, and a type-confused caller can't tell "wrong
  // type" from "nothing there").
  uint64_t UntagAllowNullHandle(TagRangeType tag_range) const {
    uint64_t content = encoded_word_;
    if (V8_LIKELY(CheckTag(content, tag_range))) {
      return (content & kPayloadMask) >> kPayloadShift;
    }
    return 0;
  }

  bool IsTaggedWithTagIn(TagRangeType tag_range) const {
    return CheckTag(encoded_word_, tag_range);
  }

  bool IsTaggedWith(TagType tag) const {
    return IsTaggedWithTagIn(TagRangeType(tag));
  }

  void SetMarkBit() { encoded_word_ |= kMarkBit; }
  void ClearMarkBit() { encoded_word_ &= ~kMarkBit; }
  bool HasMarkBitSet() const { return (encoded_word_ & kMarkBit) != 0; }

  // Extracts the freelist link if this entry is a freelist entry.
  std::optional<uint32_t> ExtractFreelistLink() const {
    if (IsTaggedWith(kFreeEntryTag)) {
      return static_cast<uint32_t>((encoded_word_ & kPayloadMask) >>
                                    kPayloadShift);
    }
    return std::nullopt;
  }

  void SetTag(TagType tag) {
    encoded_word_ =
        (encoded_word_ & ~kTagMask) | (static_cast<uint64_t>(tag) << kTagShift);
  }

  TagType ExtractTag() const {
    return static_cast<TagType>((encoded_word_ & kTagMask) >> kTagShift);
  }

  bool ContainsFreelistLink() const { return IsTaggedWith(kFreeEntryTag); }
  bool ContainsPointer() const { return !ContainsFreelistLink(); }

  bool operator==(TaggedPayload other) const {
    return encoded_word_ == other.encoded_word_;
  }
  bool operator!=(TaggedPayload other) const {
    return encoded_word_ != other.encoded_word_;
  }

 private:
  uint64_t encoded_word_;
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_TAGGED_PAYLOAD_H_
