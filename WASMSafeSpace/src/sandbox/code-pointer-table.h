// This port's CodePointerTable: real V8's CPT (src/sandbox/
// code-pointer-table.h, vendored at third_party/v8/src/sandbox/
// code-pointer-table.h, from v8 tag 12.0.267 -- upstream later replaced
// CPT's role for JS function code with the newer JSDispatchTable
// "leaptiering" mechanism, but CPT's own design is what this port
// implements: simpler, and the clearer illustration of the underlying
// control-flow-integrity idea) provides basic control-flow integrity in
// the absence of special hardware support: an indirect call/jump target is
// reached through an index into this table rather than a raw code address,
// so as long an attacker is confined to corrupting sandboxed memory, any
// indirect control transfer through a CodePointerHandle can only ever land
// on an entrypoint address that was legitimately registered via
// AllocateAndInitializeEntry/SetEntrypoint -- never on attacker-injected
// shellcode or an arbitrary sandboxed-memory address.
//
// Worth being explicit about the actual scope of that guarantee, same as
// real V8's own docs: this stops control flow from being redirected to
// non-code memory or forged addresses, but a corrupted-yet-in-bounds
// CodePointerHandle can still select a *different, still-legitimate* table
// entry (calling the wrong-but-real function). That's not a limitation of
// this port -- it's what CPT-based CFI actually promises upstream too; the
// stronger "right function, not just a function" property is what
// TrustedPointerTable's type-tag checking (see trusted-pointer-table.h)
// adds on top for a different indirection use case.
//
// Real CPT entries pack {entrypoint, code-object-pointer + mark bit in its
// LSB} into two words, relying on Code objects being pointer-aligned so the
// LSB is free for the mark bit. This port's `code_object_` is just an
// opaque `Address` handed in by the caller (see the Sandbox-integration
// test), with no alignment guarantee to reclaim a bit from -- so the mark
// bit and free-list flag are plain separate fields instead of packed into
// the pointer. Same two-word *shape* (entrypoint kept separate from the
// code object pointer, so hot call sites can load just the entrypoint) as
// real CPT; different, alignment-assumption-free bit layout.
#ifndef WASMSAFESPACE_SRC_SANDBOX_CODE_POINTER_TABLE_H_
#define WASMSAFESPACE_SRC_SANDBOX_CODE_POINTER_TABLE_H_

#include <cstdint>
#include <optional>

#include "src/sandbox/external-entity-table.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

using CodePointerHandle = uint32_t;
constexpr CodePointerHandle kNullCodePointerHandle = 0;

struct CodePointerTableEntry {
  static CodePointerTableEntry MakeFreelistEntry(uint32_t next_free_index) {
    CodePointerTableEntry entry;
    entry.is_free_ = true;
    entry.next_free_ = next_free_index;
    return entry;
  }
  static CodePointerTableEntry MakeCodePointerEntry(Address code_object,
                                                     Address entrypoint) {
    CodePointerTableEntry entry;
    entry.is_free_ = false;
    entry.code_object_ = code_object;
    entry.entrypoint_ = entrypoint;
    return entry;
  }

  bool ContainsFreelistLink() const { return is_free_; }
  std::optional<uint32_t> ExtractFreelistLink() const {
    if (is_free_) return next_free_;
    return std::nullopt;
  }

  bool is_free_ = true;
  bool marked_ = false;
  uint32_t next_free_ = 0;
  Address code_object_ = kNullAddress;
  Address entrypoint_ = kNullAddress;
};

class CodePointerTable final : public ExternalEntityTable<CodePointerTableEntry> {
 public:
  static constexpr CodePointerHandle kNullHandle = kNullCodePointerHandle;

  CodePointerTable() = default;
  CodePointerTable(const CodePointerTable&) = delete;
  CodePointerTable& operator=(const CodePointerTable&) = delete;

  CodePointerHandle AllocateAndInitializeEntry(Address code_object,
                                               Address entrypoint);

  // Returns kNullAddress for a null or out-of-range handle -- there is no
  // type tag here (see file comment), so unlike the other two tables the
  // only rejection this table can perform is a bounds check.
  Address GetEntrypoint(CodePointerHandle handle) const;
  Address GetCodeObject(CodePointerHandle handle) const;

  void SetEntrypoint(CodePointerHandle handle, Address value);
  void SetCodeObject(CodePointerHandle handle, Address value);

  void FreeEntry(CodePointerHandle handle);
  bool Contains(CodePointerHandle handle) const;

  using ExternalEntityTable::SizeForTesting;

 private:
  static uint32_t HandleToIndex(CodePointerHandle handle) { return handle - 1; }
  static CodePointerHandle IndexToHandle(uint32_t index) { return index + 1; }
};

}  // namespace internal
}  // namespace v8

#endif  // WASMSAFESPACE_SRC_SANDBOX_CODE_POINTER_TABLE_H_
