#include "src/sandbox/indirect-pointer-table.h"

#include "src/base/logging.h"

namespace v8 {
namespace internal {

IndirectPointerHandle IndirectPointerTable::AllocateAndInitializeEntry(uint64_t gpa,
                                                                        GuestPointerTag tag) {
  uint32_t index = AllocateEntry([&](IndirectPointerTableEntry& entry) {
    entry.payload_ = IndirectPointerTableEntry::Payload(gpa, tag);
  });
  return IndexToHandle(index);
}

uint64_t IndirectPointerTable::Get(IndirectPointerHandle handle,
                                    GuestPointerTagRange tag_range) const {
  if (handle == kNullHandle) return 0;
  uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index)) return 0;
  return at(index).payload_.UntagAllowNullHandle(tag_range);
}

void IndirectPointerTable::Set(IndirectPointerHandle handle, uint64_t gpa, GuestPointerTag tag) {
  DCHECK_NE(handle, kNullHandle);
  uint32_t index = HandleToIndex(handle);
  at(index).payload_ = IndirectPointerTableEntry::Payload(gpa, tag);
}

void IndirectPointerTable::FreeEntry(IndirectPointerHandle handle) {
  if (handle == kNullHandle) return;
  uint32_t index = HandleToIndex(handle);
  ExternalEntityTable::FreeEntry(index);
}

bool IndirectPointerTable::Contains(IndirectPointerHandle handle) const {
  if (handle == kNullHandle) return false;
  uint32_t index = HandleToIndex(handle);
  return IsInBounds(index) && !at(index).ContainsFreelistLink();
}

}  // namespace internal
}  // namespace v8
