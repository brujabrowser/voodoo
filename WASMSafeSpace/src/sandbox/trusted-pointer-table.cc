#include "src/sandbox/trusted-pointer-table.h"

#include "src/base/logging.h"

namespace v8 {
namespace internal {

TrustedPointerHandle TrustedPointerTable::AllocateAndInitializeEntry(
    Address pointer, IndirectPointerTag tag) {
  uint32_t index = AllocateEntry([&](TrustedPointerTableEntry& entry) {
    entry.payload_ = TrustedPointerTableEntry::Payload(pointer, tag);
  });
  return IndexToHandle(index);
}

Address TrustedPointerTable::Get(TrustedPointerHandle handle,
                                  IndirectPointerTagRange tag_range) const {
  if (handle == kNullHandle) return kNullAddress;
  uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index)) return kNullAddress;
  return static_cast<Address>(at(index).payload_.UntagAllowNullHandle(tag_range));
}

void TrustedPointerTable::Set(TrustedPointerHandle handle, Address pointer,
                               IndirectPointerTag tag) {
  DCHECK_NE(handle, kNullHandle);
  uint32_t index = HandleToIndex(handle);
  at(index).payload_ = TrustedPointerTableEntry::Payload(pointer, tag);
}

void TrustedPointerTable::FreeEntry(TrustedPointerHandle handle) {
  if (handle == kNullHandle) return;
  uint32_t index = HandleToIndex(handle);
  ExternalEntityTable::FreeEntry(index);
}

bool TrustedPointerTable::Contains(TrustedPointerHandle handle) const {
  if (handle == kNullHandle) return false;
  uint32_t index = HandleToIndex(handle);
  return IsInBounds(index) && !at(index).ContainsFreelistLink();
}

}  // namespace internal
}  // namespace v8
