// Proves the v14 codegen path -- nullable pending_remote<T>/
// pending_associated_remote<T> method parameters -- by compiling the
// generated optional_handles_interface_gen.h (from
// examples/optional_handles/optional_handles.voodoom) against real
// WASMCadidumBindings and actually sending both a present and an absent
// value of each kind over a real pipe.
#include "test.h"

#include "optional_handles_interface_gen.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class WatcherImpl : public optional_handles::Watcher {
 public:
  void OnChanged(const std::string& value) override { seen.push_back(value); }
  std::vector<std::string> seen;
};

class ListenerImpl : public optional_handles::Listener {
 public:
  void OnPing() override { ++ping_count; }
  int ping_count = 0;
};

class HubImpl : public optional_handles::Hub {
 public:
  void Watch(mojo::PendingRemote<optional_handles::Watcher> watcher)
      override {
    ++watch_calls;
    last_watcher_present = watcher.is_valid();
    if (watcher.is_valid()) {
      watcher_.Bind(std::move(watcher));
    }
  }
  void SetListener(
      mojo::PendingAssociatedRemote<optional_handles::Listener> listener)
      override {
    ++set_listener_calls;
    last_listener_present = listener.is_valid();
    if (listener.is_valid()) {
      listener_.Bind(std::move(listener));
    }
  }

  int watch_calls = 0;
  bool last_watcher_present = false;
  int set_listener_calls = 0;
  bool last_listener_present = false;
  mojo::Remote<optional_handles::Watcher> watcher_;
  mojo::AssociatedRemote<optional_handles::Listener> listener_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_nullable_pending_remote_present_transmits_real_handle) {
  HubImpl impl;
  mojo::Receiver<optional_handles::Hub> receiver(&impl);
  mojo::Remote<optional_handles::Hub> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  WatcherImpl watcher_impl;
  mojo::Receiver<optional_handles::Watcher> watcher_receiver(&watcher_impl);
  mojo::PendingRemote<optional_handles::Watcher> watcher_pending_remote;
  mojo::PendingReceiver<optional_handles::Watcher> watcher_pending_receiver =
      watcher_pending_remote.InitWithNewPipeAndPassReceiver();
  watcher_receiver.Bind(std::move(watcher_pending_receiver));

  remote->Watch(std::move(watcher_pending_remote));
  Pump();

  EXPECT_EQ(impl.watch_calls, 1);
  EXPECT(impl.last_watcher_present);
  EXPECT(impl.watcher_.is_bound());

  impl.watcher_->OnChanged("hi");
  Pump();

  EXPECT_EQ(watcher_impl.seen.size(), 1u);
  if (!watcher_impl.seen.empty()) {
    EXPECT_EQ(watcher_impl.seen[0], "hi");
  }
}

TEST(golden_nullable_pending_remote_absent_transmits_cleanly) {
  HubImpl impl;
  mojo::Receiver<optional_handles::Hub> receiver(&impl);
  mojo::Remote<optional_handles::Hub> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  // Default-constructed -- is_valid() == false -- represents "no watcher".
  remote->Watch(mojo::PendingRemote<optional_handles::Watcher>());
  Pump();

  EXPECT_EQ(impl.watch_calls, 1);
  EXPECT(!impl.last_watcher_present);
  EXPECT(!impl.watcher_.is_bound());

  // The connection must still work normally afterward -- an absent
  // handle mustn't have desynced the message stream (e.g. by still
  // consuming a handle slot on read that the writer never attached).
  remote->Watch(mojo::PendingRemote<optional_handles::Watcher>());
  Pump();
  EXPECT_EQ(impl.watch_calls, 2);
}

TEST(golden_nullable_pending_associated_remote_present_transmits_real_endpoint) {
  HubImpl impl;
  mojo::Receiver<optional_handles::Hub> receiver(&impl);
  mojo::Remote<optional_handles::Hub> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  ListenerImpl listener_impl;
  mojo::AssociatedReceiver<optional_handles::Listener> listener_receiver_obj(
      &listener_impl);
  mojo::AssociatedGroup group = remote.associated_group();
  mojo::PendingAssociatedRemote<optional_handles::Listener> listener_remote;
  mojo::PendingAssociatedReceiver<optional_handles::Listener>
      listener_receiver =
          listener_remote.InitWithNewEndpointAndPassReceiver(group);

  remote->SetListener(std::move(listener_remote));
  listener_receiver_obj.Bind(std::move(listener_receiver));
  Pump();

  EXPECT_EQ(impl.set_listener_calls, 1);
  EXPECT(impl.last_listener_present);
  EXPECT(impl.listener_.is_bound());

  impl.listener_->OnPing();
  Pump();

  EXPECT_EQ(listener_impl.ping_count, 1);
}

TEST(golden_nullable_pending_associated_remote_absent_transmits_cleanly) {
  HubImpl impl;
  mojo::Receiver<optional_handles::Hub> receiver(&impl);
  mojo::Remote<optional_handles::Hub> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SetListener(
      mojo::PendingAssociatedRemote<optional_handles::Listener>());
  Pump();

  EXPECT_EQ(impl.set_listener_calls, 1);
  EXPECT(!impl.last_listener_present);
  EXPECT(!impl.listener_.is_bound());

  remote->SetListener(
      mojo::PendingAssociatedRemote<optional_handles::Listener>());
  Pump();
  EXPECT_EQ(impl.set_listener_calls, 2);
}
