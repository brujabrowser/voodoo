// UniqueAssociatedReceiverSet<Interface>: owns N associated
// (impl, AssociatedReceiver) pairs. Matching Chromium's
// mojo/public/cpp/bindings/unique_associated_receiver_set.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_ASSOCIATED_RECEIVER_SET_H_
#define MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_ASSOCIATED_RECEIVER_SET_H_

#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "whp/base/executor.h"

#include <cstddef>
#include <map>
#include <memory>
#include <utility>

namespace mojo {

template <typename Interface>
class UniqueAssociatedReceiverSet {
 public:
  UniqueAssociatedReceiverSet() = default;
  UniqueAssociatedReceiverSet(const UniqueAssociatedReceiverSet&) = delete;
  UniqueAssociatedReceiverSet& operator=(const UniqueAssociatedReceiverSet&) =
      delete;

  void Add(std::unique_ptr<Interface> impl,
           PendingAssociatedReceiver<Interface> pending) {
    const size_t id = state_->next_id++;
    auto receiver = std::make_unique<AssociatedReceiver<Interface>>(impl.get());
    receiver->Bind(std::move(pending));
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
    std::unique_ptr<AssociatedReceiver<Interface>> receiver;
  };
  struct State {
    std::map<size_t, Entry> entries;
    size_t next_id = 1;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_UNIQUE_ASSOCIATED_RECEIVER_SET_H_
