#include "src/sandbox/sandbox.h"

#include <algorithm>
#include <cstdlib>

#if defined(_WIN32) && !defined(__wasm__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__wasm__)
#include <sys/mman.h>
#endif

namespace v8 {
namespace internal {

Sandbox* Sandbox::current_ = nullptr;

Sandbox::~Sandbox() {
  if (initialized_) TearDown();
}

void SandboxBackingDeleter::operator()(uint8_t* p) const {
  if (p == nullptr) return;
#if defined(_WIN32) && !defined(__wasm__)
  VirtualFree(p, 0, MEM_RELEASE);
#elif !defined(__wasm__)
  munmap(p, size);
#else
  delete[] p;
#endif
}

void Sandbox::Initialize(size_t size) {
  CHECK(!initialized_);
  CHECK_GT(size, 0u);
#if defined(_WIN32) && !defined(__wasm__)
  auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS));
#elif !defined(__wasm__)
  void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  auto* mem = mapped == MAP_FAILED ? nullptr : static_cast<uint8_t*>(mapped);
#else
  auto* mem = new uint8_t[size]();
#endif
  CHECK(mem != nullptr);
  backing_ = std::unique_ptr<uint8_t[], SandboxBackingDeleter>(mem, SandboxBackingDeleter{size});
  base_ = reinterpret_cast<Address>(backing_.get());
  size_ = size;
  end_ = base_ + size_;
  bump_offset_ = 0;
#if defined(_WIN32) && !defined(__wasm__)
  committed_ = 0;
#else
  committed_ = size;
#endif
  free_list_.clear();
  initialized_ = true;
}

bool Sandbox::CommitTo(size_t end) {
  if (end <= committed_) return true;
#if defined(_WIN32) && !defined(__wasm__)
  size_t target = ((end + kCommitStep - 1) / kCommitStep) * kCommitStep;
  if (target > size_) target = size_;
  if (VirtualAlloc(backing_.get() + committed_, target - committed_, MEM_COMMIT, PAGE_READWRITE) == nullptr)
    return false;
  committed_ = target;
  return true;
#else
  return end <= size_;
#endif
}

void Sandbox::TearDown() {
  CHECK(initialized_);
  if (current_ == this) current_ = nullptr;
  backing_.reset();
  base_ = kNullAddress;
  end_ = kNullAddress;
  size_ = 0;
  bump_offset_ = 0;
  committed_ = 0;
  free_list_.clear();
  initialized_ = false;
}

namespace {
size_t AlignUp(size_t v, size_t alignment) {
  return (v + alignment - 1) & ~(alignment - 1);
}
}  // namespace

void* Sandbox::AllocateFromFreeList(size_t size, size_t alignment) {
  uintptr_t base_addr = reinterpret_cast<uintptr_t>(backing_.get());
  for (size_t i = 0; i < free_list_.size(); ++i) {
    FreeBlock& b = free_list_[i];
    uintptr_t block_start = base_addr + b.offset;
    uintptr_t aligned = AlignUp(block_start, alignment);
    size_t head = aligned - block_start;
    if (head + size > b.size) {
      continue;  // Doesn't fit once aligned, even though b.size alone did.
    }
    size_t tail = b.size - head - size;
    size_t block_offset = b.offset;
    free_list_.erase(free_list_.begin() + static_cast<long>(i));
    // Re-insert whatever's left on either side of the carved-out
    // [aligned, aligned+size) span, so a request that doesn't consume a
    // free block exactly still leaves the remainder reclaimable.
    if (head > 0) {
      free_list_.push_back(FreeBlock{block_offset, head});
    }
    if (tail > 0) {
      free_list_.push_back(FreeBlock{block_offset + head + size, tail});
    }
    std::sort(free_list_.begin(), free_list_.end(),
              [](const FreeBlock& a, const FreeBlock& c) { return a.offset < c.offset; });
    return backing_.get() + (block_offset + head);
  }
  return nullptr;
}

void* Sandbox::Allocate(size_t size, size_t alignment) {
  void* result = TryAllocate(size, alignment);
  CHECK(result != nullptr);
  return result;
}

void* Sandbox::TryAllocate(size_t size, size_t alignment) {
  CHECK(initialized_);
  if (void* reused = AllocateFromFreeList(size, alignment)) {
    return reused;
  }
  // Align the *absolute* address, not just the running offset: the cage's
  // own base address has no alignment guarantee beyond whatever the host
  // allocator happened to hand back for `backing_` (see this method's file
  // history -- an earlier version aligned `bump_offset_` alone, which only
  // produced a correctly-aligned pointer by coincidence when `base()`
  // itself happened to already be aligned; wasmtime's own allocator
  // returned a `base()` that wasn't, so aligned_offset=64 tests started
  // failing under wasm32-wasip1 -- see the top-level README's own
  // "real bug" section for the same "only actually running on the target
  // catches this" story).
  uintptr_t base_addr = reinterpret_cast<uintptr_t>(backing_.get());
  uintptr_t unaligned = base_addr + bump_offset_;
  uintptr_t aligned = AlignUp(unaligned, alignment);
  size_t aligned_offset = aligned - base_addr;
  if (aligned_offset > size_ || size > size_ - aligned_offset) return nullptr;
  if (!CommitTo(aligned_offset + size)) return nullptr;
  void* result = backing_.get() + aligned_offset;
  bump_offset_ = aligned_offset + size;
  return result;
}

void Sandbox::Free(void* ptr, size_t size) {
  CHECK(initialized_);
  if (!ptr || size == 0) return;
  CHECK(Contains(ptr));
  size_t offset = static_cast<size_t>(reinterpret_cast<uintptr_t>(ptr) -
                                       reinterpret_cast<uintptr_t>(backing_.get()));
  // Insert address-sorted, then coalesce with whichever neighbor(s) this
  // block now touches -- keeps the free list from fragmenting into many
  // small blocks under alloc/free churn (exactly the per-message pattern
  // this was added for).
  auto it = std::lower_bound(
      free_list_.begin(), free_list_.end(), offset,
      [](const FreeBlock& b, size_t o) { return b.offset < o; });
  it = free_list_.insert(it, FreeBlock{offset, size});
  if (it + 1 != free_list_.end() && it->offset + it->size == (it + 1)->offset) {
    it->size += (it + 1)->size;
    free_list_.erase(it + 1);
  }
  if (it != free_list_.begin()) {
    auto prev = it - 1;
    if (prev->offset + prev->size == it->offset) {
      prev->size += it->size;
      free_list_.erase(it);
    }
  }
}

}  // namespace internal
}  // namespace v8
