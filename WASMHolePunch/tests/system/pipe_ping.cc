#include "test.h"

#include "whp/c/system.h"

#include <cstring>
#include <string>

TEST(MessagePipePing) {
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a, &b), WHP_RESULT_OK);

  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessage(nullptr, &msg), WHP_RESULT_OK);
  void* buf = nullptr;
  uint32_t sz = 0;
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  const char* hello = "hello";
  EXPECT_EQ(WhpAppendMessageData(msg, 5, nullptr, 0, &opts, &buf, &sz),
            WHP_RESULT_OK);
  std::memcpy(buf, hello, 5);

  EXPECT_EQ(WhpWriteMessage(a, msg, nullptr), WHP_RESULT_OK);

  WhpHandleSignalsState st{};
  EXPECT_EQ(WhpQueryHandleSignalsState(b, &st), WHP_RESULT_OK);
  EXPECT(st.satisfied_signals & WHP_HANDLE_SIGNAL_READABLE);

  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpReadMessage(b, nullptr, &got), WHP_RESULT_OK);
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(WhpGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr),
            WHP_RESULT_OK);
  EXPECT_EQ(rn, 5u);
  EXPECT(std::string(static_cast<char*>(rbuf), 5) == "hello");

  EXPECT_EQ(WhpDestroyMessage(got), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(a), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(b), WHP_RESULT_OK);
}

TEST(DataPipePing) {
  WhpHandle prod = WHP_HANDLE_INVALID;
  WhpHandle cons = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateDataPipe(nullptr, &prod, &cons), WHP_RESULT_OK);
  uint32_t n = 4;
  EXPECT_EQ(WhpWriteData(prod, "abcd", &n, 0), WHP_RESULT_OK);
  EXPECT_EQ(n, 4u);
  char buf[8] = {};
  n = 4;
  EXPECT_EQ(WhpReadData(cons, buf, &n, 0), WHP_RESULT_OK);
  EXPECT_EQ(n, 4u);
  EXPECT(std::string(buf, 4) == "abcd");
  EXPECT_EQ(WhpClose(prod), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(cons), WHP_RESULT_OK);
}

TEST(SharedBufferMap) {
  WhpHandle buf = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateSharedBuffer(64, nullptr, &buf), WHP_RESULT_OK);
  void* p = nullptr;
  EXPECT_EQ(WhpMapBuffer(buf, 0, 64, &p), WHP_RESULT_OK);
  EXPECT(p != nullptr);
  std::memcpy(p, "xyz", 3);
  WhpHandle dup = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpDuplicateBufferHandle(buf, &dup), WHP_RESULT_OK);
  void* p2 = nullptr;
  EXPECT_EQ(WhpMapBuffer(dup, 0, 64, &p2), WHP_RESULT_OK);
  EXPECT(std::memcmp(p2, "xyz", 3) == 0);
  EXPECT_EQ(WhpUnmapBuffer(p), WHP_RESULT_OK);
  EXPECT_EQ(WhpUnmapBuffer(p2), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(buf), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(dup), WHP_RESULT_OK);
}
