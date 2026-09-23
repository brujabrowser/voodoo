#include "src/sandbox/cppheap-pointer-table.h"

#include "src/base/logging.h"

namespace cppgc {
namespace internal {

v8::CppHeapPointerHandle CppHeapPointerTable::AllocateAndInitializeEntry(
    void* pointer, v8::CppHeapPointerTag tag) {
  uint32_t index = AllocateEntry([&](CppHeapPointerTableEntry& entry) {
    entry.payload_ = CppHeapPointerTableEntry::Payload(
        reinterpret_cast<uint64_t>(pointer), tag);
  });
  return IndexToHandle(index);
}

void* CppHeapPointerTable::Get(v8::CppHeapPointerHandle handle,
                               v8::CppHeapPointerTagRange tag_range) const {
  if (handle == kNullHandle) return nullptr;
  const uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index)) return nullptr;
  const uint64_t payload = at(index).payload_.UntagAllowNullHandle(tag_range);
  return reinterpret_cast<void*>(static_cast<uintptr_t>(payload));
}

void CppHeapPointerTable::Set(v8::CppHeapPointerHandle handle, void* pointer,
                              v8::CppHeapPointerTag tag) {
  CHECK_NE(handle, kNullHandle);
  const uint32_t index = HandleToIndex(handle);
  at(index).payload_ = CppHeapPointerTableEntry::Payload(
      reinterpret_cast<uint64_t>(pointer), tag);
}

void CppHeapPointerTable::FreeEntry(v8::CppHeapPointerHandle handle) {
  if (handle == kNullHandle) return;
  const uint32_t index = HandleToIndex(handle);
  ExternalEntityTable::FreeEntry(index);
}

bool CppHeapPointerTable::Contains(v8::CppHeapPointerHandle handle) const {
  if (handle == kNullHandle) return false;
  const uint32_t index = HandleToIndex(handle);
  return IsInBounds(index) && !at(index).ContainsFreelistLink();
}

}  // namespace internal
}  // namespace cppgc
