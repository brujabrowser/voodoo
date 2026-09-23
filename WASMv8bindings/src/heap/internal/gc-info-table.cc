// Strip-ported from V8's src/heap/cppgc-internal/gc-info-table.cc. See the
// header's file comment for what changed (SetPermissions/read-only
// recommit is a real no-op on this port's DefaultPageAllocator, so the
// recommit-as-read-only dance upstream does in Resize() is dropped -- the
// growth algorithm itself is unchanged).
#include "src/heap/internal/gc-info-table.h"

#include <algorithm>
#include <limits>

#include "cppgc/internal/gc-info.h"

namespace cppgc {
namespace internal {

GCInfoTable* GlobalGCInfoTable::global_table_ = nullptr;

// static
void GlobalGCInfoTable::Initialize(PageAllocator& page_allocator) {
  static GCInfoTable table(page_allocator, GetGlobalOOMHandler());
  if (!global_table_) {
    global_table_ = &table;
  } else {
    CHECK_EQ(&page_allocator, &global_table_->allocator());
  }
}

GCInfoTable::GCInfoTable(PageAllocator& page_allocator,
                         FatalOutOfMemoryHandler& oom_handler)
    : page_allocator_(page_allocator), oom_handler_(oom_handler) {
  Resize();
}

GCInfoTable::~GCInfoTable() {
  page_allocator_.ReleasePages(table_, MaxTableSize(), 0);
}

size_t GCInfoTable::MaxTableSize() const { return kMaxIndex * sizeof(GCInfo); }

GCInfoIndex GCInfoTable::InitialTableLimit() const {
  return kInitialWantedLimit;
}

void GCInfoTable::Resize() {
  const GCInfoIndex new_limit = limit_ ? 2 * limit_ : InitialTableLimit();
  CHECK_GT(new_limit, limit_);
  const GCInfoIndex clamped_limit = std::min(new_limit, kMaxIndex);
  CHECK_GT(clamped_limit, limit_);

  GCInfo* new_table = static_cast<GCInfo*>(page_allocator_.AllocatePages(
      nullptr, clamped_limit * sizeof(GCInfo), page_allocator_.AllocatePageSize(),
      PageAllocator::kReadWrite));
  if (!new_table) {
    oom_handler_("Oilpan: GCInfoTable resize.");
  }
  if (table_) {
    std::copy(table_, table_ + limit_, new_table);
    page_allocator_.FreePages(table_, limit_ * sizeof(GCInfo));
  }
  table_ = new_table;
  limit_ = clamped_limit;
}

GCInfoIndex GCInfoTable::RegisterNewGCInfo(
    std::atomic<GCInfoIndex>& registered_index, const GCInfo& info) {
  v8::base::MutexGuard guard(&table_mutex_);

  const GCInfoIndex index = registered_index.load(std::memory_order_relaxed);
  if (index) return index;

  if (current_index_ == limit_) {
    Resize();
  }

  const GCInfoIndex new_index = current_index_++;
  CHECK_LT(new_index, kMaxIndex);
  table_[new_index] = info;
  registered_index.store(new_index, std::memory_order_release);
  return new_index;
}

}  // namespace internal
}  // namespace cppgc
