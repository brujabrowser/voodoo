#include "test.h"

#include "mojo/public/c/system/core.h"

#include <cstring>
#include <string>

TEST(MessagePipeRoundtrip) {
  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);

  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const char* hello = "hello";
  EXPECT_EQ(MojoAppendMessageData(msg, 5, nullptr, 0, &opts, &buf, &sz),
            MOJO_RESULT_OK);
  std::memcpy(buf, hello, 5);

  EXPECT_EQ(MojoWriteMessage(a, msg, nullptr), MOJO_RESULT_OK);

  MojoHandleSignalsState state{};
  EXPECT_EQ(MojoQueryHandleSignalsState(b, &state), MOJO_RESULT_OK);
  EXPECT(state.satisfied_signals & MOJO_HANDLE_SIGNAL_READABLE);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoReadMessage(b, nullptr, &got), MOJO_RESULT_OK);
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(MojoGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(rn, 5u);
  EXPECT(std::string(static_cast<char*>(rbuf), 5) == "hello");

  EXPECT_EQ(MojoDestroyMessage(got), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(a), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(b), MOJO_RESULT_OK);
}

TEST(FuseMessagePipes) {
  MojoHandle a0 = MOJO_HANDLE_INVALID;
  MojoHandle a1 = MOJO_HANDLE_INVALID;
  MojoHandle b0 = MOJO_HANDLE_INVALID;
  MojoHandle b1 = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &a0, &a1), MOJO_RESULT_OK);
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &b0, &b1), MOJO_RESULT_OK);
  EXPECT_EQ(MojoFuseMessagePipes(a1, b1, nullptr), MOJO_RESULT_OK);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);
  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  EXPECT_EQ(MojoAppendMessageData(msg, 2, nullptr, 0, &opts, &buf, &sz),
            MOJO_RESULT_OK);
  std::memcpy(buf, "ok", 2);
  EXPECT_EQ(MojoWriteMessage(a0, msg, nullptr), MOJO_RESULT_OK);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoReadMessage(b0, nullptr, &got), MOJO_RESULT_OK);
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(MojoGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(rn, 2u);
  EXPECT_EQ(MojoDestroyMessage(got), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(a0), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(b0), MOJO_RESULT_OK);
}

TEST(SetQueryQuotaUnreadCount) {
  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);
  EXPECT_EQ(MojoSetQuota(b, MOJO_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT, 1, nullptr),
            MOJO_RESULT_OK);
  uint64_t limit = 0;
  uint64_t usage = 0;
  EXPECT_EQ(MojoQueryQuota(b, MOJO_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT, nullptr,
                           &limit, &usage),
            MOJO_RESULT_OK);
  EXPECT_EQ(limit, 1u);
  EXPECT_EQ(MojoClose(a), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(b), MOJO_RESULT_OK);
}

TEST(GetBufferInfo) {
  MojoHandle buf = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateSharedBuffer(32, nullptr, &buf), MOJO_RESULT_OK);
  MojoSharedBufferInfo info{};
  info.struct_size = sizeof(info);
  EXPECT_EQ(MojoGetBufferInfo(buf, nullptr, &info), MOJO_RESULT_OK);
  EXPECT_EQ(info.num_bytes, 32u);
  EXPECT_EQ(MojoClose(buf), MOJO_RESULT_OK);
}

TEST(DefaultProcessErrorHandlerSeesBadMessage) {
  static bool saw = false;
  saw = false;
  EXPECT_EQ(MojoSetDefaultProcessErrorHandler(
                [](const char*, uint32_t) { saw = true; }, nullptr),
            MOJO_RESULT_OK);
  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);
  EXPECT_EQ(MojoNotifyBadMessage(msg, "x", 1, nullptr), MOJO_RESULT_OK);
  EXPECT(saw);
  EXPECT_EQ(MojoSetDefaultProcessErrorHandler(nullptr, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(MojoDestroyMessage(msg), MOJO_RESULT_OK);
}

TEST(BadMessageIsNonFatal) {
  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);
  const char* reason = "header too short";
  EXPECT_EQ(MojoNotifyBadMessage(msg, reason, static_cast<uint32_t>(std::strlen(reason)),
                                 nullptr),
            MOJO_RESULT_OK);

  EXPECT_EQ(MojoDestroyMessage(msg), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(a), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(b), MOJO_RESULT_OK);
}
