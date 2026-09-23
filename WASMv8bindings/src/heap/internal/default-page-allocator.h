// This port's PageAllocator: wasm32-wasip1 linear memory has no OS-level
// page protection to manage, so unlike real V8's mmap-backed allocators,
// this one is just new[]/delete[] with every permission request accepted
// as a no-op (see v8-platform.h's file comment for why that's honest, not
// a shortcut: there is nothing on this target that WASM's own memory
// sandboxing doesn't already cover).
#ifndef WASMV8_SRC_HEAP_INTERNAL_DEFAULT_PAGE_ALLOCATOR_H_
#define WASMV8_SRC_HEAP_INTERNAL_DEFAULT_PAGE_ALLOCATOR_H_

#include <cstdint>
#include <new>

#include "v8-platform.h"

namespace cppgc {
namespace internal {

class DefaultPageAllocator final : public v8::PageAllocator {
 public:
  size_t AllocatePageSize() override { return 4096; }

  void* AllocatePages(void*, size_t length, size_t, Permission) override {
    return new (std::nothrow) uint8_t[length];
  }

  bool FreePages(void* address, size_t) override {
    delete[] reinterpret_cast<uint8_t*>(address);
    return true;
  }

  bool ReleasePages(void* address, size_t, size_t new_length) override {
    if (new_length == 0) {
      delete[] reinterpret_cast<uint8_t*>(address);
    }
    // Partial release (new_length > 0): keep the whole block resident.
    // Wastes some memory versus a real OS decommit; never a correctness
    // issue since nothing here relies on the trailing bytes being
    // inaccessible.
    return true;
  }

  bool SetPermissions(void*, size_t, Permission) override { return true; }
  bool DecommitPages(void*, size_t) override { return true; }
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_DEFAULT_PAGE_ALLOCATOR_H_
