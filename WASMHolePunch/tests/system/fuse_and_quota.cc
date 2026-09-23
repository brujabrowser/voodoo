#include "test.h"

#include "whp/c/system.h"

#include <cstring>

namespace {

WhpResult WriteBytes(WhpHandle pipe, const char* s) {
  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  WhpResult r = WhpCreateMessage(nullptr, &msg);
  if (r != WHP_RESULT_OK) return r;
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const uint32_t n = static_cast<uint32_t>(std::strlen(s));
  r = WhpAppendMessageData(msg, n, nullptr, 0, &opts, &buf, &sz);
  if (r != WHP_RESULT_OK) return r;
  std::memcpy(buf, s, n);
  return WhpWriteMessage(pipe, msg, nullptr);
}

bool ReadEquals(WhpHandle pipe, const char* s) {
  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  if (WhpReadMessage(pipe, nullptr, &got) != WHP_RESULT_OK) return false;
  void* buf = nullptr;
  uint32_t n = 0;
  if (WhpGetMessageData(got, nullptr, &buf, &n, nullptr, nullptr) !=
      WHP_RESULT_OK) {
    return false;
  }
  const uint32_t want = static_cast<uint32_t>(std::strlen(s));
  bool ok = n == want && std::memcmp(buf, s, want) == 0;
  WhpDestroyMessage(got);
  return ok;
}

}  // namespace

TEST(FuseMessagePipesSplicesPeers) {
  WhpHandle a0 = WHP_HANDLE_INVALID;
  WhpHandle a1 = WHP_HANDLE_INVALID;
  WhpHandle b0 = WHP_HANDLE_INVALID;
  WhpHandle b1 = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a0, &a1), WHP_RESULT_OK);
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &b0, &b1), WHP_RESULT_OK);
  EXPECT_EQ(WhpFuseMessagePipes(a1, b1), WHP_RESULT_OK);
  EXPECT_EQ(WriteBytes(a0, "hi"), WHP_RESULT_OK);
  EXPECT(ReadEquals(b0, "hi"));
  EXPECT_EQ(WriteBytes(b0, "yo"), WHP_RESULT_OK);
  EXPECT(ReadEquals(a0, "yo"));
  EXPECT_EQ(WhpClose(a0), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(b0), WHP_RESULT_OK);
}

TEST(QuotaUnreadCountBlocksWriter) {
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a, &b), WHP_RESULT_OK);
  EXPECT_EQ(WhpSetQuota(b, WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT, 1),
            WHP_RESULT_OK);
  uint64_t limit = 0;
  uint64_t usage = 99;
  EXPECT_EQ(WhpQueryQuota(b, WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT, &limit,
                          &usage),
            WHP_RESULT_OK);
  EXPECT_EQ(limit, 1u);
  EXPECT_EQ(usage, 0u);
  EXPECT_EQ(WriteBytes(a, "one"), WHP_RESULT_OK);
  EXPECT_EQ(WhpQueryQuota(b, WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT, &limit,
                          &usage),
            WHP_RESULT_OK);
  EXPECT_EQ(usage, 1u);
  EXPECT_EQ(WriteBytes(a, "two"), WHP_RESULT_RESOURCE_EXHAUSTED);
  EXPECT(ReadEquals(b, "one"));
  EXPECT_EQ(WriteBytes(a, "two"), WHP_RESULT_OK);
  EXPECT(ReadEquals(b, "two"));
  EXPECT_EQ(WhpClose(a), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(b), WHP_RESULT_OK);
}
