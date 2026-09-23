// Single-thread DefaultPlatform for this port. Real V8's
// include/cppgc/default-platform.h wraps libplatform's thread pool;
// wasm32-wasip1 has no threads, so PageAllocator + clocks live here and
// PostJob / GetForegroundTaskRunner inherit Platform's calling-thread
// DefaultJobHandle (Join/Cancel/IsActive/NotifyConcurrencyIncrease).
#ifndef INCLUDE_CPPGC_DEFAULT_PLATFORM_H_
#define INCLUDE_CPPGC_DEFAULT_PLATFORM_H_

#include "cppgc/platform.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {

class V8_EXPORT DefaultPlatform : public Platform {
 public:
  DefaultPlatform();
  ~DefaultPlatform() override;

  PageAllocator* GetPageAllocator() override;
  double MonotonicallyIncreasingTime() override;
};

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_DEFAULT_PLATFORM_H_
