// Direct tests of interface_control_messages.h's two helpers, independent
// of any generated Proxy_/Stub_ -- proves the wire-level primitive works
// on its own, the same way connector_roundtrip.cc's SyncWaitFor tests do
// for that primitive.
#include "test.h"

#include "mojo/public/cpp/bindings/lib/interface_control_messages.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

#include <utility>

using namespace mojo;
using namespace mojo::internal;

namespace {

void Pump(int iterations = 4) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

// A minimal MessageReceiver that only understands the two control
// ordinals -- stands in for what a generated Stub_::Accept()'s control-
// message interception looks like, without needing any generated code.
class ControlOnlyReceiver : public MessageReceiver {
 public:
  ControlOnlyReceiver(MultiplexRouter* router, uint32_t version)
      : router_(router), version_(version) {}

  bool Accept(Message* message) override {
    if (message->name() == kRunMessageId) {
      return HandleQueryVersionMessage(message, router_, kPrimaryInterfaceId,
                                        version_);
    }
    if (message->name() == kRunOrClosePipeMessageId) {
      return HandleRequireVersionMessage(*message, version_);
    }
    return false;
  }

 private:
  MultiplexRouter* router_;
  uint32_t version_;
};

class VersionCapturingReceiver : public MessageReceiver {
 public:
  bool Accept(Message* message) override {
    size_t offset = 0;
    uint32_t tag = 0;
    ok = ReadScalar(*message, &offset, &tag) &&
         tag == kRunQueryVersionResult &&
         ReadScalar(*message, &offset, &version);
    return true;
  }

  bool ok = false;
  uint32_t version = 0xFFFFFFFFu;
};

}  // namespace

TEST(control_query_version_response_carries_responders_version) {
  ScopedMessagePipeHandle a, b;
  CreateMessagePipe(nullptr, &a, &b);
  auto client_router = MultiplexRouter::Create(std::move(a), false);
  auto server_router = MultiplexRouter::Create(std::move(b), false);

  ControlOnlyReceiver server(server_router.get(), /*version=*/7);
  server_router->AttachPrimaryClient(&server);
  server_router->StartReceiving();

  VersionCapturingReceiver client;
  client_router->AttachPrimaryClient(&client);
  client_router->StartReceiving();

  Message request(kRunMessageId, Message::kFlagExpectsResponse,
                   kPrimaryInterfaceId);
  request.set_request_id(1);
  EXPECT(client_router->SendMessage(&request));

  Pump();

  EXPECT(client.ok);
  EXPECT_EQ(client.version, 7u);
}

TEST(control_query_version_message_id_is_reserved_max_uint32) {
  EXPECT_EQ(kRunMessageId, 0xFFFFFFFFu);
  EXPECT_EQ(kRunOrClosePipeMessageId, 0xFFFFFFFEu);
}

TEST(control_require_version_accepts_when_responder_version_is_sufficient) {
  Message m(kRunOrClosePipeMessageId, 0, kPrimaryInterfaceId);
  uint32_t tag = kRunRequireVersion;
  uint32_t required = 3;
  WriteScalar(&m, tag);
  WriteScalar(&m, required);
  EXPECT(HandleRequireVersionMessage(m, /*version=*/5));
}

TEST(control_require_version_accepts_when_versions_match_exactly) {
  Message m(kRunOrClosePipeMessageId, 0, kPrimaryInterfaceId);
  uint32_t tag = kRunRequireVersion;
  uint32_t required = 5;
  WriteScalar(&m, tag);
  WriteScalar(&m, required);
  EXPECT(HandleRequireVersionMessage(m, /*version=*/5));
}

TEST(control_require_version_rejects_when_responder_version_is_too_low) {
  Message m(kRunOrClosePipeMessageId, 0, kPrimaryInterfaceId);
  uint32_t tag = kRunRequireVersion;
  uint32_t required = 10;
  WriteScalar(&m, tag);
  WriteScalar(&m, required);
  EXPECT(!HandleRequireVersionMessage(m, /*version=*/5));
}

TEST(control_require_version_rejects_truncated_message) {
  Message m(kRunOrClosePipeMessageId, 0, kPrimaryInterfaceId);
  // No payload written at all -- ReadScalar must fail cleanly, not read
  // garbage.
  EXPECT(!HandleRequireVersionMessage(m, /*version=*/5));
}
