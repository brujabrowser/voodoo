#include "test.h"

#include "whp/c/system.h"
#include "whp/message.h"
#include "whp/message_header_validator.h"
#include "whp/system/message_pipe.h"

#include <cstring>
#include <string>

TEST(MessagePayloadRoundTrip) {
  whp::MessagePipe pipe;
  EXPECT(pipe.handle0.is_valid());
  EXPECT(pipe.handle1.is_valid());

  whp::Message msg(7, 0);
  const char* payload = "wasm";
  void* p = msg.AllocatePayload(4);
  std::memcpy(p, payload, 4);
  EXPECT_EQ(msg.name(), 7u);
  EXPECT_EQ(msg.version(), 3u);
  EXPECT_EQ(msg.payload_num_bytes(), 4u);

  whp::MessageHeaderValidator v;
  EXPECT(v.Accept(&msg));

  WhpMessageHandle h = msg.TakeMojoMessage();
  EXPECT(msg.IsNull());
  EXPECT_EQ(WhpWriteMessage(pipe.handle0.get(), h, nullptr), WHP_RESULT_OK);

  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpReadMessage(pipe.handle1.get(), nullptr, &got), WHP_RESULT_OK);
  whp::Message incoming = whp::Message::CreateFromMessageHandle(&got);
  EXPECT(!incoming.IsNull());
  EXPECT_EQ(incoming.name(), 7u);
  EXPECT_EQ(incoming.version(), 3u);
  EXPECT_EQ(incoming.payload_num_bytes(), 4u);
  EXPECT(std::string(reinterpret_cast<const char*>(incoming.payload()), 4) ==
         "wasm");
  EXPECT(v.Accept(&incoming));
}

TEST(TrapFiresOnWrite) {
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessagePipe(nullptr, &a, &b), WHP_RESULT_OK);

  static int fired = 0;
  fired = 0;
  auto handler = [](const WhpTrapEvent* ev) {
    if (ev->result == WHP_RESULT_OK) {
      ++fired;
    }
  };
  WhpHandle trap = WHP_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateTrap(handler, nullptr, &trap), WHP_RESULT_OK);
  EXPECT_EQ(WhpAddTrigger(trap, b, WHP_HANDLE_SIGNAL_READABLE,
                          WHP_TRIGGER_CONDITION_SIGNALS_SATISFIED, 123,
                          nullptr),
            WHP_RESULT_OK);
  EXPECT_EQ(WhpArmTrap(trap, nullptr, nullptr, nullptr), WHP_RESULT_OK);

  WhpMessageHandle msg = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpCreateMessage(nullptr, &msg), WHP_RESULT_OK);
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  WhpAppendMessageData(msg, 1, nullptr, 0, &opts, &buf, &sz);
  static_cast<char*>(buf)[0] = 'x';
  EXPECT_EQ(WhpWriteMessage(a, msg, nullptr), WHP_RESULT_OK);
  // Trap handlers no longer fire synchronously nested inside the call
  // that satisfied their trigger (WhpWriteMessage here) -- see
  // WhpPumpEvents' own doc comment in whp/c/system.h. An explicit pump
  // is what delivers it now, same as whp::Executor::RunUntilIdle()
  // already does automatically for every other consumer in this family.
  EXPECT_EQ(fired, 0);
  WhpPumpEvents();
  EXPECT_EQ(fired, 1);

  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(WhpReadMessage(b, nullptr, &got), WHP_RESULT_OK);
  WhpDestroyMessage(got);
  WhpClose(trap);
  WhpClose(a);
  WhpClose(b);
}
