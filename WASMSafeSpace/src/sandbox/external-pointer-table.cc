#include "src/sandbox/external-pointer-table.h"

#include "src/base/logging.h"

namespace v8 {
namespace internal {

ExternalPointerHandle ExternalPointerTable::AllocateAndInitializeEntry(
    void* pointer, ExternalPointerTag tag) {
  uint32_t index = AllocateEntry([&](ExternalPointerTableEntry& entry) {
    entry.payload_ = ExternalPointerTableEntry::Payload(
        reinterpret_cast<uint64_t>(pointer), tag);
  });
  return IndexToHandle(index);
}

void* ExternalPointerTable::Get(ExternalPointerHandle handle,
                                 ExternalPointerTagRange tag_range) const {
  if (handle == kNullHandle) return nullptr;
  uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index)) return nullptr;
  uint64_t raw = at(index).payload_.UntagAllowNullHandle(tag_range);
  return reinterpret_cast<void*>(static_cast<uintptr_t>(raw));
}

void ExternalPointerTable::Set(ExternalPointerHandle handle, void* pointer,
                                ExternalPointerTag tag) {
  DCHECK_NE(handle, kNullHandle);
  uint32_t index = HandleToIndex(handle);
  at(index).payload_ = ExternalPointerTableEntry::Payload(
      reinterpret_cast<uint64_t>(pointer), tag);
}

void ExternalPointerTable::FreeEntry(ExternalPointerHandle handle) {
  if (handle == kNullHandle) return;
  ExternalEntityTable::FreeEntry(HandleToIndex(handle));
}

bool ExternalPointerTable::Contains(ExternalPointerHandle handle) const {
  if (handle == kNullHandle) return false;
  uint32_t index = HandleToIndex(handle);
  return IsInBounds(index) && !at(index).ContainsFreelistLink();
}

}  // namespace internal
}  // namespace v8
