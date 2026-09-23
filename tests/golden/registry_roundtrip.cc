// Proves the v2 codegen paths -- enum, struct (by-value param + inside an
// array response), array<string>, and a non-associated pending_remote<T>
// handle handoff -- by compiling the generated registry_interface_gen.h
// (from examples/registry/registry.voodoom) against real WASMCadidumBindings
// and actually running it.
#include "test.h"

#include "registry_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class WatcherImpl : public registry::Watcher {
 public:
  void OnChanged(const v8::internal::CageVector<std::string>& keys) override {
    seen = keys;
  }
  v8::internal::CageVector<std::string> seen;
};

class StoreImpl : public registry::Store {
 public:
  void Put(const registry::Entry& entry,
           base::OnceCallback<void(bool)> callback) override {
    entries_.push_back(entry);
    callback(true);
  }
  void List(base::OnceCallback<void(v8::internal::CageVector<registry::Entry>)> callback)
      override {
    callback(entries_);
  }
  void Watch(mojo::PendingRemote<registry::Watcher> watcher) override {
    watcher_.Bind(std::move(watcher));
    if (watcher_.is_bound()) {
      watcher_->OnChanged({"triggered"});
    }
  }

 private:
  v8::internal::CageVector<registry::Entry> entries_;
  mojo::Remote<registry::Watcher> watcher_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_struct_enum_array_roundtrip) {
  StoreImpl impl;
  mojo::Receiver<registry::Store> receiver(&impl);
  mojo::Remote<registry::Store> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  registry::Entry e;
  e.key = "a";
  e.value = 42;
  e.status = registry::Status::OK;

  bool put_ok = false;
  bool put_done = false;
  remote->Put(e, [&](bool ok) {
    put_ok = ok;
    put_done = true;
  });
  Pump();
  EXPECT(put_done);
  EXPECT(put_ok);

  v8::internal::CageVector<registry::Entry> got;
  bool list_done = false;
  remote->List([&](v8::internal::CageVector<registry::Entry> entries) {
    got = entries;
    list_done = true;
  });
  Pump();

  EXPECT(list_done);
  EXPECT_EQ(got.size(), 1u);
  if (!got.empty()) {
    EXPECT_EQ(got[0].key, "a");
    EXPECT_EQ(got[0].value, 42);
    EXPECT(got[0].status == registry::Status::OK);
  }
}

TEST(golden_non_associated_pending_remote_handoff) {
  StoreImpl impl;
  mojo::Receiver<registry::Store> receiver(&impl);
  mojo::Remote<registry::Store> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  WatcherImpl watcher_impl;
  mojo::Receiver<registry::Watcher> watcher_receiver(&watcher_impl);

  // A fresh, real pipe -- unlike pending_associated_remote (in-process
  // bookkeeping on an existing router), this one is a genuine
  // ScopedMessagePipeHandle attached to the wire message and reconstructed
  // on the other side via Message::TakeHandles().
  mojo::PendingRemote<registry::Watcher> watcher_pending_remote;
  mojo::PendingReceiver<registry::Watcher> watcher_pending_receiver =
      watcher_pending_remote.InitWithNewPipeAndPassReceiver();
  watcher_receiver.Bind(std::move(watcher_pending_receiver));

  remote->Watch(std::move(watcher_pending_remote));
  Pump();

  EXPECT_EQ(watcher_impl.seen.size(), 1u);
  if (!watcher_impl.seen.empty()) {
    EXPECT_EQ(watcher_impl.seen[0], "triggered");
  }
}
