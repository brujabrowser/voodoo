#include "test.h"

#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "whp/base/executor.h"

#include <cstring>

using mojo::Connector;
using mojo::Message;
using mojo::MessageReceiver;

namespace {

class Recorder : public MessageReceiver {
 public:
  [[nodiscard]] bool Accept(Message* message) override {
    last_name = message->name();
    last_payload.assign(
        message->payload(), message->payload() + message->payload_num_bytes());
    ++count;
    return true;
  }

  int count = 0;
  uint32_t last_name = 0;
  std::vector<uint8_t> last_payload;
};

void Pump(int iterations = 4) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(connector_send_and_receive) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  Connector sender(std::move(a));
  Connector receiver(std::move(b));

  Recorder recorder;
  receiver.set_incoming_receiver(&recorder);
  receiver.StartReceiving();

  Message message(/*name=*/11, /*flags=*/0);
  message.WritePayload("payload!", 8);
  EXPECT(sender.Accept(&message));

  Pump();

  EXPECT_EQ(recorder.count, 1);
  EXPECT_EQ(recorder.last_name, 11u);
  EXPECT_EQ(recorder.last_payload.size(), 8u);
  EXPECT(std::memcmp(recorder.last_payload.data(), "payload!", 8) == 0);
}

TEST(connector_sync_wait_for_returns_true_once_predicate_becomes_true) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  Connector sender(std::move(a));
  Connector receiver(std::move(b));

  Recorder recorder;
  receiver.set_incoming_receiver(&recorder);
  receiver.StartReceiving();

  Message message(/*name=*/7, /*flags=*/0);
  message.WritePayload("hi", 2);
  EXPECT(sender.Accept(&message));

  // Deliberately no manual Pump() first -- SyncWaitFor must do its own
  // pumping, the whole point of the primitive.
  bool ok = receiver.SyncWaitFor([&] { return recorder.count > 0; });
  EXPECT(ok);
  EXPECT_EQ(recorder.count, 1);
}

TEST(connector_sync_wait_for_returns_immediately_if_already_true) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  Connector receiver(std::move(b));
  // Never calls StartReceiving() -- if this pumped at all when the
  // predicate is already satisfied, there'd be nothing armed to pump.
  bool ok = receiver.SyncWaitFor([] { return true; });
  EXPECT(ok);
}

TEST(connector_sync_wait_for_times_out) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  Connector receiver(std::move(b));
  receiver.StartReceiving();
  bool ok = receiver.SyncWaitFor([] { return false; },
                                 base::TimeDelta::FromMilliseconds(5));
  EXPECT(!ok);
}

TEST(connector_sync_wait_for_returns_false_on_peer_closed) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  Connector receiver(std::move(b));
  receiver.StartReceiving();

  a.reset();  // close the peer -- the predicate itself never goes true

  bool ok = receiver.SyncWaitFor([] { return false; });
  EXPECT(!ok);
  EXPECT(receiver.encountered_error());
}

TEST(connector_peer_closed_raises_error) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  Connector receiver(std::move(b));
  bool error_fired = false;
  receiver.set_connection_error_handler([&error_fired] { error_fired = true; });
  receiver.StartReceiving();

  a.reset();  // close the peer

  Pump();

  EXPECT(error_fired);
  EXPECT(receiver.encountered_error());
}
