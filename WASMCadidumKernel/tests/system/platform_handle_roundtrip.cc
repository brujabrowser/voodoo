#include "test.h"

#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/platform_handle.h"

#include <cstdint>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

MojoPlatformHandle MakeNative() {
  MojoPlatformHandle ph{};
  ph.struct_size = sizeof(ph);
#ifdef _WIN32
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ph.type = MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE;
  ph.value = reinterpret_cast<uint64_t>(ev);
#else
  int fds[2];
  pipe(fds);
  close(fds[1]);
  ph.type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
  ph.value = static_cast<uint64_t>(fds[0]);
#endif
  return ph;
}

void CloseNative(const MojoPlatformHandle& ph) {
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ph.value)));
#else
  close(static_cast<int>(ph.value));
#endif
}

}  // namespace

TEST(platform_handle_wrap_unwrap) {
  MojoPlatformHandle native = MakeNative();
  mojo::PlatformHandle wrapped = mojo::PlatformHandle::Wrap(native);
  EXPECT(wrapped.is_valid());
  MojoPlatformHandle out{};
  EXPECT(std::move(wrapped).Unwrap(&out));
  EXPECT_EQ(out.value, native.value);
  CloseNative(out);
}

TEST(wrap_platform_shared_memory_region_mach_token) {
  MojoPlatformHandle ph{};
  ph.struct_size = sizeof(ph);
  ph.type = MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT;
  ph.value = 11;
  MojoHandle h = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoWrapPlatformSharedMemoryRegion(
                &ph, 1, 8, nullptr,
                MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE, nullptr,
                &h),
            MOJO_RESULT_OK);
  void* data = nullptr;
  EXPECT_EQ(MojoMapBuffer(h, 0, 8, nullptr, &data), MOJO_RESULT_OK);
  static_cast<uint8_t*>(data)[1] = 0x22;
  EXPECT_EQ(MojoUnmapBuffer(data), MOJO_RESULT_OK);
  MojoPlatformHandle out{};
  out.struct_size = sizeof(out);
  uint32_t n = 1;
  uint64_t bytes = 0;
  MojoSharedBufferGuid guid{};
  MojoPlatformSharedMemoryRegionAccessMode mode = 0;
  EXPECT_EQ(MojoUnwrapPlatformSharedMemoryRegion(h, nullptr, &out, &n, &bytes,
                                                 &guid, &mode),
            MOJO_RESULT_OK);
  EXPECT_EQ(out.value, 11u);
  EXPECT_EQ(bytes, 8u);
}

TEST(platform_handle_wrap_unwrap_mach_send_right) {
  MojoPlatformHandle native{};
  native.struct_size = sizeof(native);
  native.type = MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT;
  native.value = 42;
  mojo::PlatformHandle wrapped = mojo::PlatformHandle::Wrap(native);
  EXPECT(wrapped.is_valid());
  MojoPlatformHandle out{};
  EXPECT(std::move(wrapped).Unwrap(&out));
  EXPECT_EQ(out.type, MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT);
  EXPECT_EQ(out.value, 42u);
}
