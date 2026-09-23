// Minimal stand-in for real V8's include/v8-internal.h -- just the handful
// of things the sandbox headers below actually need: the `Address` typedef
// (real V8 also just types this as `uintptr_t`), the KB/MB/GB byte-count
// constants used throughout the sandbox/table code for readability, and
// `TagRange<Tag>` (copied verbatim from upstream -- see its own comment).
//
// Real v8-internal.h is a ~2000-line grab-bag covering object layout,
// isolate internals, and a dozen unrelated subsystems; none of that belongs
// in a sandbox-only port, so this file is written fresh rather than
// strip-ported, same convention as ../WASMv8bindings/include/v8-sandbox.h's
// own note about folding TagRange in locally instead of mirroring the full
// file.
#ifndef WASMSAFESPACE_INCLUDE_V8_INTERNAL_H_
#define WASMSAFESPACE_INCLUDE_V8_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "v8config.h"

namespace v8 {

// Real V8: an address in the process's virtual address space. On this port's
// wasm32-wasip1 target this is a 32-bit offset into the single linear
// memory; on a native build it's a real 32/64-bit pointer value. Either way,
// exactly what real V8 does with `Address = uintptr_t`.
using Address = uintptr_t;
constexpr Address kNullAddress = 0;

namespace internal {

constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024;
constexpr size_t GB = MB * 1024;

// Verbatim from V8's include/v8-internal.h -- a closed range [first, last]
// over a uint16_t-backed enum, with a fast branchless-friendly Contains().
// Already strip-ported once for this family, see
// ../WASMv8bindings/include/v8-sandbox.h; reproduced here unchanged since
// this port needs it for more than one tag type (IndirectPointerTag,
// ExternalPointerTag) and so it earns its own shared header this time.
template <typename Tag>
struct TagRange {
  static_assert(std::is_enum_v<Tag> &&
                    std::is_same_v<std::underlying_type_t<Tag>, uint16_t>,
                "Tag parameter must be an enum with base type uint16_t");

  constexpr TagRange(Tag first, Tag last) : first(first), last(last) {}
  constexpr TagRange(Tag tag) : first(tag), last(tag) {}  // NOLINT
  constexpr TagRange() : TagRange(static_cast<Tag>(0)) {}

  constexpr bool IsEmpty() const {
    return first == static_cast<Tag>(0) && last == static_cast<Tag>(0);
  }
  constexpr size_t Size() const {
    if (IsEmpty()) return 0;
    return static_cast<uint32_t>(last) - static_cast<uint32_t>(first) + 1;
  }
  constexpr bool Contains(Tag tag) const {
    return static_cast<uint32_t>(tag) - static_cast<uint32_t>(first) <=
           static_cast<uint32_t>(last) - static_cast<uint32_t>(first);
  }
  constexpr bool Contains(TagRange other) const {
    return other.first >= first && other.last <= last;
  }
  constexpr bool operator==(const TagRange& other) const {
    return first == other.first && last == other.last;
  }

  Tag first;
  Tag last;
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_INCLUDE_V8_INTERNAL_H_
