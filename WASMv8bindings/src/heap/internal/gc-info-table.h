// Strip-ported from V8's src/heap/cppgc-internal/gc-info-table.h --
// genuinely near-verbatim. Real cppgc reserves the full kMaxIndex table
// upfront via mmap and recommits read-only/read-write sub-ranges as it
// grows, a security-hardening measure (an attacker who corrupts a
// GCInfoIndex can't also forge new trace/finalize function pointers into
// read-only memory). This port's DefaultPageAllocator makes
// SetPermissions() a no-op (see its file comment), so that hardening
// doesn't apply here, but the table's actual growth algorithm --
// exponential resize, one process-wide singleton, mutex-guarded
// registration -- is unchanged from upstream.
#ifndef WASMV8_SRC_HEAP_INTERNAL_GC_INFO_TABLE_H_
#define WASMV8_SRC_HEAP_INTERNAL_GC_INFO_TABLE_H_

#include <stdint.h>

#include "cppgc/internal/gc-info.h"
#include "cppgc/platform.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/heap/internal/platform.h"

namespace cppgc {
namespace internal {

class V8_EXPORT GCInfoTable final {
 public:
  static constexpr GCInfoIndex kMaxIndex = 1 << 14;
  static constexpr GCInfoIndex kMinIndex = 1;
  static constexpr GCInfoIndex kInitialWantedLimit = 512;

  GCInfoTable(PageAllocator& page_allocator, FatalOutOfMemoryHandler& oom_handler);
  ~GCInfoTable();
  GCInfoTable(const GCInfoTable&) = delete;
  GCInfoTable& operator=(const GCInfoTable&) = delete;

  GCInfoIndex RegisterNewGCInfo(std::atomic<uint16_t>&, const GCInfo& info);

  const GCInfo& GCInfoFromIndex(GCInfoIndex index) const {
    DCHECK_GE(index, kMinIndex);
    DCHECK_LT(index, kMaxIndex);
    DCHECK(table_);
    return table_[index];
  }

  GCInfoIndex NumberOfGCInfos() const { return current_index_; }
  PageAllocator& allocator() const { return page_allocator_; }

 private:
  void Resize();
  GCInfoIndex InitialTableLimit() const;
  size_t MaxTableSize() const;

  PageAllocator& page_allocator_;
  FatalOutOfMemoryHandler& oom_handler_;
  GCInfo* table_ = nullptr;
  GCInfoIndex current_index_ = kMinIndex;
  GCInfoIndex limit_ = 0;
  v8::base::Mutex table_mutex_;
};

class V8_EXPORT GlobalGCInfoTable final {
 public:
  GlobalGCInfoTable(const GlobalGCInfoTable&) = delete;
  GlobalGCInfoTable& operator=(const GlobalGCInfoTable&) = delete;

  static void Initialize(PageAllocator& page_allocator);

  static GCInfoTable& GetMutable() { return *global_table_; }
  static const GCInfoTable& Get() { return *global_table_; }
  static const GCInfo& GCInfoFromIndex(GCInfoIndex index) {
    return Get().GCInfoFromIndex(index);
  }

 private:
  static GCInfoTable* global_table_;
  GlobalGCInfoTable() = delete;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_GC_INFO_TABLE_H_
