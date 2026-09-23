#include "test.h"

#include "whp/c/system.h"

#include <cstring>

namespace {

struct Ctx {
  bool serialized = false;
  bool destroyed = false;
};

void Serialize(uintptr_t context, WhpMessageHandle message) {
  auto* ctx = reinterpret_cast<Ctx*>(context);
  ctx->serialized = true;
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  WhpAppendMessageData(message, 4, nullptr, 0, &opts, &buf, &sz);
  std::memcpy(buf, "ctx!", 4);
}

void Destroy(uintptr_t context) {
  reinterpret_cast<Ctx*>(context)->destroyed = true;
}

}  // namespace

TEST(MessageContextSerializesOnWrite) {
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a, &b), WHP_RESULT_OK);
  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessage(nullptr, &msg), WHP_RESULT_OK);
  Ctx ctx;
  EXPECT_EQ(WhpSetMessageContext(msg, reinterpret_cast<uintptr_t>(&ctx),
                                 Serialize, Destroy),
            WHP_RESULT_OK);
  uintptr_t got = 0;
  EXPECT_EQ(WhpGetMessageContext(msg, &got), WHP_RESULT_OK);
  EXPECT_EQ(got, reinterpret_cast<uintptr_t>(&ctx));
  EXPECT_EQ(WhpWriteMessage(a, msg, nullptr), WHP_RESULT_OK);
  EXPECT(ctx.serialized);
  EXPECT(ctx.destroyed);

  WhpMessageHandle in = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpReadMessage(b, nullptr, &in), WHP_RESULT_OK);
  void* buf = nullptr;
  uint32_t n = 0;
  EXPECT_EQ(WhpGetMessageData(in, nullptr, &buf, &n, nullptr, nullptr),
            WHP_RESULT_OK);
  EXPECT_EQ(n, 4u);
  EXPECT_EQ(WhpDestroyMessage(in), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(a), WHP_RESULT_OK);
  EXPECT_EQ(WhpClose(b), WHP_RESULT_OK);
}

TEST(ReserveMessageCapacityGrowsBuffer) {
  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessage(nullptr, &msg), WHP_RESULT_OK);
  uint32_t sz = 0;
  EXPECT_EQ(WhpReserveMessageCapacity(msg, 64, &sz), WHP_RESULT_OK);
  EXPECT_EQ(sz, 64u);
  EXPECT_EQ(WhpDestroyMessage(msg), WHP_RESULT_OK);
}

TEST(GetBufferInfoReportsSize) {
  WhpHandle buf = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateSharedBuffer(128, nullptr, &buf), WHP_RESULT_OK);
  uint64_t n = 0;
  EXPECT_EQ(WhpGetBufferInfo(buf, &n), WHP_RESULT_OK);
  EXPECT_EQ(n, 128u);
  EXPECT_EQ(WhpClose(buf), WHP_RESULT_OK);
}
