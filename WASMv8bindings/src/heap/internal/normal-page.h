// This port's NormalPage: a fixed-size (kPageSize, matching real cppgc's
// own 128KiB page size from globals.h) block that the Heap's bump-pointer
// allocator carves objects out of. Real cppgc's NormalPage embeds its own
// C++ header at the front of the same OS-page-aligned allocation, which is
// what lets `BasePage::FromPayload(ptr)` recover a page from any payload
// pointer by just masking off the low `kPageSizeLog2` bits (see
// third_party/v8/src/heap/cppgc-internal/heap-page.h). This port never
// needs that lookup (no BasePage-derived heap-membership checks, no
// conservative/inner-pointer scanning -- see the top-level README), so
// NormalPage is a plain, separately-heap-allocated C++ object pointing at
// its own payload block instead. Same page size, same bump-pointer +
// FreeList allocation algorithm (see heap.h), just without the
// address-masking trick.
//
// Sandbox composition: when a v8::internal::Sandbox is passed in (i.e.
// Sandbox::current() is non-null -- see heap.cc's call site), the payload
// block comes from Sandbox::Allocate() instead of a plain new[], so every
// cppgc-managed object a normal (non-large) allocation produces genuinely
// lives inside the cage -- the composition both this repo's and
// ../../WASMSafeSpace's READMEs describe as the missing wiring between
// cppgc and the Sandbox. Sandbox::Allocate() has no Free() (see its own
// file comment), so a sandbox-backed page's bytes are never reclaimed even
// once Destroy() is called on an empty page -- the small NormalPage C++
// object itself still is (it's a separate, non-sandbox allocation), just
// not the kPageSize payload. This is an accepted tradeoff, not an oversight:
// the same "no table-level reclamation, only caller-managed freeing"
// simplification both repos already document for CppHeapPointerTable/
// ExternalEntityTable. Sandbox::Allocate() also CHECK-fails (hard abort) on
// exhaustion rather than returning null -- matching real V8's own
// treatment of cage exhaustion as fatal, but different from this port's
// prior always-graceful new(std::nothrow) path; a long-running sandboxed
// Heap that churns through many empty pages can exhaust Sandbox::kDefaultSize
// (64MB / 128KiB-per-page ~= 512 pages) faster than an unsandboxed one would
// hit real OOM.
#ifndef WASMV8_SRC_HEAP_INTERNAL_NORMAL_PAGE_H_
#define WASMV8_SRC_HEAP_INTERNAL_NORMAL_PAGE_H_

#include <new>

#include "src/heap/internal/globals.h"
#include "src/sandbox/sandbox.h"

namespace cppgc {
namespace internal {

class NormalPage final {
 public:
  static NormalPage* Create(v8::internal::Sandbox* sandbox) {
    uint8_t* memory;
    if (sandbox) {
      memory = static_cast<uint8_t*>(sandbox->Allocate(kPageSize, kPageSize));
    } else {
      memory = new (std::nothrow) uint8_t[kPageSize];
      if (!memory) return nullptr;
    }
    return new NormalPage(memory, sandbox != nullptr);
  }

  static void Destroy(NormalPage* page) {
    if (!page->sandboxed_) delete[] page->memory_;
    delete page;
  }

  Address PayloadStart() const { return memory_; }
  Address PayloadEnd() const { return memory_ + PayloadSize(); }
  // Strictly less than kPageSize: FreeList buckets by
  // WhichPowerOfTwo(RoundDownToPowerOfTwo32(size)), sized to
  // kPageSizeLog2 entries on the assumption (real cppgc's own,
  // see FreeList::Add's DCHECK_GT(kPageSize, size)) that no free
  // block ever reaches a full page in size. Real cppgc's NormalPage
  // gets this for free because its page header lives inside the same
  // OS-page-aligned allocation as the payload; this port's NormalPage
  // is a separate object (see class comment) with nothing to naturally
  // shrink the payload, so one allocation-granularity's worth is
  // reserved here purely to preserve that invariant -- never allocated
  // into, and small enough it's not a meaningful density loss.
  static constexpr size_t PayloadSize() { return kPageSize - kAllocationGranularity; }

 private:
  NormalPage(uint8_t* memory, bool sandboxed)
      : memory_(memory), sandboxed_(sandboxed) {}
  ~NormalPage() = default;

  uint8_t* memory_;
  bool sandboxed_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_NORMAL_PAGE_H_
