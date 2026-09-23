#include "src/sandbox/code-pointer-table.h"

#include "src/base/logging.h"

namespace v8 {
namespace internal {

CodePointerHandle CodePointerTable::AllocateAndInitializeEntry(
    Address code_object, Address entrypoint) {
  uint32_t index = AllocateEntry([&](CodePointerTableEntry& entry) {
    entry = CodePointerTableEntry::MakeCodePointerEntry(code_object, entrypoint);
  });
  return IndexToHandle(index);
}

Address CodePointerTable::GetEntrypoint(CodePointerHandle handle) const {
  if (handle == kNullHandle) return kNullAddress;
  uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index) || at(index).is_free_) return kNullAddress;
  return at(index).entrypoint_;
}

Address CodePointerTable::GetCodeObject(CodePointerHandle handle) const {
  if (handle == kNullHandle) return kNullAddress;
  uint32_t index = HandleToIndex(handle);
  if (!IsInBounds(index) || at(index).is_free_) return kNullAddress;
  return at(index).code_object_;
}

void CodePointerTable::SetEntrypoint(CodePointerHandle handle, Address value) {
  DCHECK_NE(handle, kNullHandle);
  at(HandleToIndex(handle)).entrypoint_ = value;
}

void CodePointerTable::SetCodeObject(CodePointerHandle handle, Address value) {
  DCHECK_NE(handle, kNullHandle);
  at(HandleToIndex(handle)).code_object_ = value;
}

void CodePointerTable::FreeEntry(CodePointerHandle handle) {
  if (handle == kNullHandle) return;
  ExternalEntityTable::FreeEntry(HandleToIndex(handle));
}

bool CodePointerTable::Contains(CodePointerHandle handle) const {
  if (handle == kNullHandle) return false;
  uint32_t index = HandleToIndex(handle);
  return IsInBounds(index) && !at(index).is_free_;
}

}  // namespace internal
}  // namespace v8
