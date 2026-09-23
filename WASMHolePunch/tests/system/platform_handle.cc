#include "test.h"

#include "whp/c/system.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool MakeNative(WhpPlatformHandleType* type, uint64_t* value) {
#ifdef _WIN32
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!ev) return false;
  *type = WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE;
  *value = reinterpret_cast<uint64_t>(ev);
  return true;
#else
  int fds[2];
  if (pipe(fds) != 0) return false;
  close(fds[1]);
  *type = WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
  *value = static_cast<uint64_t>(fds[0]);
  return true;
#endif
}

void CloseNative(WhpPlatformHandleType type, uint64_t value) {
#ifdef _WIN32
  (void)type;
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)));
#else
  (void)type;
  close(static_cast<int>(value));
#endif
}

}  // namespace

TEST(PlatformHandleWrapUnwrapRoundtrip) {
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  EXPECT(MakeNative(&type, &value));

  WhpHandle h = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformHandle(type, value, &h), WHP_RESULT_OK);
  EXPECT(h != WHP_HANDLE_INVALID);

  WhpPlatformHandleType out_type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t out_value = 0;
  EXPECT_EQ(WhpUnwrapPlatformHandle(h, &out_type, &out_value), WHP_RESULT_OK);
  EXPECT_EQ(out_type, type);
  EXPECT_EQ(out_value, value);
  CloseNative(out_type, out_value);
}

TEST(PlatformHandleCloseClosesNative) {
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  EXPECT(MakeNative(&type, &value));
  WhpHandle h = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformHandle(type, value, &h), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(h), WHP_RESULT_OK);
#ifdef _WIN32
  DWORD flags = 0;
  EXPECT(!GetHandleInformation(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)), &flags));
#endif
}

TEST(PlatformHandleTransitsAMessagePipe) {
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a, &b), WHP_RESULT_OK);

  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  EXPECT(MakeNative(&type, &value));
  WhpHandle wrapped = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformHandle(type, value, &wrapped), WHP_RESULT_OK);

  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessage(nullptr, &msg), WHP_RESULT_OK);
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  EXPECT_EQ(WhpAppendMessageData(msg, 0, &wrapped, 1, &opts, &buf, &sz),
            WHP_RESULT_OK);
  EXPECT_EQ(WhpWriteMessage(a, msg, nullptr), WHP_RESULT_OK);

  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpReadMessage(b, nullptr, &got), WHP_RESULT_OK);
  WhpHandle received = WHP_HANDLE_INVALID;
  uint32_t nhandles = 1;
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(WhpGetMessageData(got, nullptr, &rbuf, &rn, &received, &nhandles),
            WHP_RESULT_OK);
  EXPECT_EQ(nhandles, 1u);

  WhpPlatformHandleType out_type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t out_value = 0;
  EXPECT_EQ(WhpUnwrapPlatformHandle(received, &out_type, &out_value),
            WHP_RESULT_OK);
  EXPECT_EQ(out_value, value);
  CloseNative(out_type, out_value);

  EXPECT_EQ(WhpDestroyMessage(got), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(a), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(b), WHP_RESULT_OK);
}

TEST(PlatformHandleRejectsWrongHostType) {
  WhpHandle h = WHP_HANDLE_INVALID;
#ifdef _WIN32
  EXPECT_EQ(WhpWrapPlatformHandle(WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR, 1,
                                  &h),
            WHP_RESULT_INVALID_ARGUMENT);
#else
  EXPECT_EQ(WhpWrapPlatformHandle(WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE, 1,
                                  &h),
            WHP_RESULT_INVALID_ARGUMENT);
#endif
}

TEST(PlatformHandleMachSendRightRoundtrip) {
  WhpHandle h = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpWrapPlatformHandle(WHP_PLATFORM_HANDLE_TYPE_MACH_PORT, 42, &h),
            WHP_RESULT_OK);
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  EXPECT_EQ(WhpUnwrapPlatformHandle(h, &type, &value), WHP_RESULT_OK);
  EXPECT_EQ(type, WHP_PLATFORM_HANDLE_TYPE_MACH_PORT);
  EXPECT_EQ(value, 42u);
}
