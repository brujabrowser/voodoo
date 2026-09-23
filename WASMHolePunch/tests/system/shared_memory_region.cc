#include "test.h"

#include "whp/c/system.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

TEST(SharedMemoryRegionMachTokenMapsInProcess) {
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_MACH_PORT;
  uint64_t value = 7;
  WhpHandle h = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformSharedMemoryRegion(
                &type, &value, 1, 16, 1, 2,
                WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE, &h),
            WHP_RESULT_OK);

  void* data = nullptr;
  EXPECT_EQ(WhpMapBuffer(h, 0, 16, &data), WHP_RESULT_OK);
  EXPECT(data != nullptr);
  static_cast<uint8_t*>(data)[0] = 0x5A;
  EXPECT_EQ(WhpUnmapBuffer(data), WHP_RESULT_OK);

  WhpPlatformHandleType out_type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t out_value = 0;
  uint32_t n = 1;
  uint64_t bytes = 0;
  uint64_t gh = 0;
  uint64_t gl = 0;
  WhpPlatformSharedMemoryRegionAccessMode mode = 0;
  EXPECT_EQ(WhpUnwrapPlatformSharedMemoryRegion(h, &out_type, &out_value, &n,
                                                &bytes, &gh, &gl, &mode),
            WHP_RESULT_OK);
  EXPECT_EQ(out_type, WHP_PLATFORM_HANDLE_TYPE_MACH_PORT);
  EXPECT_EQ(out_value, 7u);
  EXPECT_EQ(bytes, 16u);
  EXPECT_EQ(gh, 1u);
  EXPECT_EQ(gl, 2u);
}

#ifdef _WIN32
TEST(SharedMemoryRegionWindowsHandleMaps) {
  HANDLE mapping =
      CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096,
                         nullptr);
  EXPECT(mapping != nullptr);
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE;
  uint64_t value = reinterpret_cast<uint64_t>(mapping);
  WhpHandle h = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformSharedMemoryRegion(
                &type, &value, 1, 4096, 0, 0,
                WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE, &h),
            WHP_RESULT_OK);

  void* data = nullptr;
  EXPECT_EQ(WhpMapBuffer(h, 0, 4096, &data), WHP_RESULT_OK);
  static_cast<uint8_t*>(data)[0] = 0xAB;
  EXPECT_EQ(WhpUnmapBuffer(data), WHP_RESULT_OK);

  WhpHandle dup = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpDuplicateBufferHandle(h, &dup), WHP_RESULT_OK);
  void* data2 = nullptr;
  EXPECT_EQ(WhpMapBuffer(dup, 0, 4096, &data2), WHP_RESULT_OK);
  EXPECT_EQ(static_cast<uint8_t*>(data2)[0], 0xAB);
  EXPECT_EQ(WhpUnmapBuffer(data2), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(dup), WHP_RESULT_OK);

  WhpPlatformHandleType out_type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t out_value = 0;
  uint32_t n = 1;
  uint64_t bytes = 0;
  uint64_t gh = 0;
  uint64_t gl = 0;
  WhpPlatformSharedMemoryRegionAccessMode mode = 0;
  EXPECT_EQ(WhpUnwrapPlatformSharedMemoryRegion(h, &out_type, &out_value, &n,
                                                &bytes, &gh, &gl, &mode),
            WHP_RESULT_OK);
  EXPECT_EQ(out_value, value);
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(out_value)));
}
#endif
