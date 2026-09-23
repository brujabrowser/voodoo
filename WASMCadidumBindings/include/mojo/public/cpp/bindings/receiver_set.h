// ReceiverSet<Interface>: many Receivers bound to one impl (the impl is
// not owned). Peer disconnect drops that receiver. Matching Chromium's
// mojo/public/cpp/bindings/receiver_set.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_SET_H_
#define MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_SET_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "whp/base/executor.h"

#include <cstddef>
#include <map>
#include <memory>
#include <utility>

namespace mojo {

using ReceiverSetID = size_t;

template <typename Interface>
class ReceiverSet {
 public:
  ReceiverSet() = default;
  ReceiverSet(const ReceiverSet&) = delete;
  ReceiverSet& operator=(const ReceiverSet&) = delete;

  ReceiverSetID Add(Interface* impl, PendingReceiver<Interface> pending) {
    const ReceiverSetID id = state_->next_id++;
    auto receiver = std::make_unique<Receiver<Interface>>(impl);
    receiver->Bind(std::move(pending));
    receiver->set_disconnect_handler([state = state_, id]() {
      whp::Executor::Current().PostTask(
          whp::BindOnce([state, id]() { state->entries.erase(id); }));
    });
    state_->entries[id] = std::move(receiver);
    return id;
  }

  void Remove(ReceiverSetID id) { state_->entries.erase(id); }
  void Clear() { state_->entries.clear(); }
  size_t size() const { return state_->entries.size(); }
  bool empty() const { return state_->entries.empty(); }

 private:
  struct State {
    std::map<ReceiverSetID, std::unique_ptr<Receiver<Interface>>> entries;
    ReceiverSetID next_id = 1;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_RECEIVER_SET_H_
