#include "test.h"

#include "mojo/public/c/system/core.h"

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

TEST(MojoWrapUnwrapPlatformHandle) {
  MojoPlatformHandle native = MakeNative();
  EXPECT(native.value != 0);
  MojoHandle mh = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoWrapPlatformHandle(&native, nullptr, &mh), MOJO_RESULT_OK);
  EXPECT(mh != MOJO_HANDLE_INVALID);

  MojoPlatformHandle out{};
  out.struct_size = sizeof(out);
  EXPECT_EQ(MojoUnwrapPlatformHandle(mh, nullptr, &out), MOJO_RESULT_OK);
  EXPECT_EQ(out.type, native.type);
  EXPECT_EQ(out.value, native.value);
  CloseNative(out);
}

TEST(MojoWrapPlatformHandleTransitsAPipe) {
  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);

  MojoPlatformHandle native = MakeNative();
  MojoHandle wrapped = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoWrapPlatformHandle(&native, nullptr, &wrapped), MOJO_RESULT_OK);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);
  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  EXPECT_EQ(MojoAppendMessageData(msg, 0, &wrapped, 1, &opts, &buf, &sz),
            MOJO_RESULT_OK);
  EXPECT_EQ(MojoWriteMessage(a, msg, nullptr), MOJO_RESULT_OK);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoReadMessage(b, nullptr, &got), MOJO_RESULT_OK);
  MojoHandle received = MOJO_HANDLE_INVALID;
  uint32_t nhandles = 1;
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(MojoGetMessageData(got, nullptr, &rbuf, &rn, &received, &nhandles),
            MOJO_RESULT_OK);

  MojoPlatformHandle out{};
  out.struct_size = sizeof(out);
  EXPECT_EQ(MojoUnwrapPlatformHandle(received, nullptr, &out), MOJO_RESULT_OK);
  EXPECT_EQ(out.value, native.value);
  CloseNative(out);

  EXPECT_EQ(MojoDestroyMessage(got), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(a), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(b), MOJO_RESULT_OK);
}

TEST(MojoWrapMachSendRight) {
  MojoPlatformHandle ph{};
  ph.struct_size = sizeof(ph);
  ph.type = MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT;
  ph.value = 42;
  MojoHandle mh = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoWrapPlatformHandle(&ph, nullptr, &mh), MOJO_RESULT_OK);
  MojoPlatformHandle out{};
  out.struct_size = sizeof(out);
  EXPECT_EQ(MojoUnwrapPlatformHandle(mh, nullptr, &out), MOJO_RESULT_OK);
  EXPECT_EQ(out.type, MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT);
  EXPECT_EQ(out.value, 42u);
}

TEST(MojoWrapPlatformSharedMemoryRegionMachToken) {
  MojoPlatformHandle ph{};
  ph.struct_size = sizeof(ph);
  ph.type = MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT;
  ph.value = 9;
  MojoSharedBufferGuid guid{3, 4};
  MojoHandle mh = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoWrapPlatformSharedMemoryRegion(
                &ph, 1, 32, &guid,
                MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE, nullptr,
                &mh),
            MOJO_RESULT_OK);

  void* data = nullptr;
  EXPECT_EQ(MojoMapBuffer(mh, 0, 32, nullptr, &data), MOJO_RESULT_OK);
  static_cast<uint8_t*>(data)[0] = 0x11;
  EXPECT_EQ(MojoUnmapBuffer(data), MOJO_RESULT_OK);

  MojoPlatformHandle out{};
  out.struct_size = sizeof(out);
  uint32_t n = 1;
  uint64_t bytes = 0;
  MojoSharedBufferGuid gout{};
  MojoPlatformSharedMemoryRegionAccessMode mode = 0;
  EXPECT_EQ(MojoUnwrapPlatformSharedMemoryRegion(mh, nullptr, &out, &n, &bytes,
                                                 &gout, &mode),
            MOJO_RESULT_OK);
  EXPECT_EQ(out.value, 9u);
  EXPECT_EQ(bytes, 32u);
  EXPECT_EQ(gout.high, 3u);
  EXPECT_EQ(gout.low, 4u);
}
