// Ported verbatim (module-path edits only) from V8's
// src/heap/cppgc-internal/gc-info.cc.
#include "cppgc/internal/gc-info.h"

#include "cppgc/internal/name-trait.h"
#include "src/heap/internal/gc-info-table.h"

namespace cppgc::internal {

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    FinalizationCallback finalization_callback, NameCallback name_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index,
      GCInfo(finalization_callback, trace_callback, name_callback));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    FinalizationCallback finalization_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index,
      GCInfo(finalization_callback, trace_callback, GetHiddenName));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    NameCallback name_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index, GCInfo(nullptr, trace_callback, name_callback));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index, GCInfo(nullptr, trace_callback, GetHiddenName));
}

}  // namespace cppgc::internal
