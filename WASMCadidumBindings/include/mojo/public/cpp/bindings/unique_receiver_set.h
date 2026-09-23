// UniqueReceiverSet<Interface>: owns N (impl, Receiver) pairs. Adding a
// pending receiver takes ownership of the impl; a peer disconnect removes
// that entry. Matching Chromium's
// mojo/public/cpp/bindings/unique_receiver_set.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_RECEIVER_SET_H_
#define MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_RECEIVER_SET_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "whp/base/executor.h"

#include <cstddef>
#include <map>
#include <memory>
#include <utility>

namespace mojo {

template <typename Interface>
class UniqueReceiverSet {
 public:
  UniqueReceiverSet() = default;
  UniqueReceiverSet(const UniqueReceiverSet&) = delete;
  UniqueReceiverSet& operator=(const UniqueReceiverSet&) = delete;

  void Add(std::unique_ptr<Interface> impl,
           PendingReceiver<Interface> pending_receiver) {
    const size_t id = state_->next_id++;
    auto receiver = std::make_unique<Receiver<Interface>>(impl.get());
    receiver->Bind(std::move(pending_receiver));
    receiver->set_disconnect_handler([state = state_, id]() {
      whp::Executor::Current().PostTask(
          whp::BindOnce([state, id]() { state->entries.erase(id); }));
    });
    state_->entries[id] = Entry{std::move(impl), std::move(receiver)};
  }

  void Clear() { state_->entries.clear(); }
  size_t size() const { return state_->entries.size(); }
  bool empty() const { return state_->entries.empty(); }

 private:
  struct Entry {
    std::unique_ptr<Interface> impl;
    std::unique_ptr<Receiver<Interface>> receiver;
  };
  struct State {
    std::map<size_t, Entry> entries;
    size_t next_id = 1;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_RECEIVER_SET_H_
