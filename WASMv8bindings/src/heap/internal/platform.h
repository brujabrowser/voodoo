// Strip-ported from V8's src/heap/cppgc-internal/platform.h -- portable
// as-is, just FatalOutOfMemoryHandler plus process-global OOM-handler and
// PageAllocator accessors.
#ifndef WASMV8_SRC_HEAP_INTERNAL_PLATFORM_H_
#define WASMV8_SRC_HEAP_INTERNAL_PLATFORM_H_

#include <string>

#include "cppgc/platform.h"
#include "cppgc/source-location.h"
#include "src/base/macros.h"

namespace cppgc {
namespace internal {

// Heap-identity + OOM diagnostics. This port's Heap inherits it so a
// heap-bound FatalOutOfMemoryHandler can report which heap died and how
// full it was -- the same HeapBase* V8 passes to a custom OOM callback.
class HeapBase {
 public:
  virtual ~HeapBase() = default;
  virtual size_t normal_page_count() const { return 0; }
  virtual size_t large_object_count() const { return 0; }
};

class V8_EXPORT_PRIVATE FatalOutOfMemoryHandler final {
 public:
  using Callback = void(const std::string&, SourceLocation, HeapBase*);

  FatalOutOfMemoryHandler() = default;
  explicit FatalOutOfMemoryHandler(HeapBase* heap) : heap_(heap) {}

  [[noreturn]] void operator()(
      const std::string& reason = std::string(),
      SourceLocation = SourceLocation::Current()) const;

  void SetCustomHandler(Callback* callback) { custom_handler_ = callback; }

  FatalOutOfMemoryHandler(const FatalOutOfMemoryHandler&) = delete;
  FatalOutOfMemoryHandler& operator=(const FatalOutOfMemoryHandler&) = delete;

 private:
  HeapBase* heap_ = nullptr;
  Callback* custom_handler_ = nullptr;
};

FatalOutOfMemoryHandler& GetGlobalOOMHandler();
PageAllocator& GetGlobalPageAllocator();

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_PLATFORM_H_
