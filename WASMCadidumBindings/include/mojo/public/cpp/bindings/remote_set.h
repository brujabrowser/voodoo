// RemoteSet<Interface>: owns N Remotes. Peer disconnect drops that remote.
// Matching Chromium's mojo/public/cpp/bindings/remote_set.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_REMOTE_SET_H_
#define MOJO_PUBLIC_CPP_BINDINGS_REMOTE_SET_H_

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

#include <cstddef>
#include <map>
#include <memory>
#include <utility>

namespace mojo {

using RemoteSetElementId = size_t;

template <typename Interface>
class RemoteSet {
 public:
  RemoteSet() = default;
  RemoteSet(const RemoteSet&) = delete;
  RemoteSet& operator=(const RemoteSet&) = delete;

  RemoteSetElementId Add(PendingRemote<Interface> pending) {
    const RemoteSetElementId id = state_->next_id++;
    auto remote = std::make_unique<Remote<Interface>>();
    remote->Bind(std::move(pending));
    remote->set_disconnect_handler([state = state_, id]() {
      whp::Executor::Current().PostTask(
          whp::BindOnce([state, id]() { state->entries.erase(id); }));
    });
    state_->entries[id] = std::move(remote);
    return id;
  }

  Remote<Interface>* Get(RemoteSetElementId id) {
    auto it = state_->entries.find(id);
    return it == state_->entries.end() ? nullptr : it->second.get();
  }

  void Remove(RemoteSetElementId id) { state_->entries.erase(id); }
  void Clear() { state_->entries.clear(); }
  size_t size() const { return state_->entries.size(); }
  bool empty() const { return state_->entries.empty(); }

 private:
  struct State {
    std::map<RemoteSetElementId, std::unique_ptr<Remote<Interface>>> entries;
    RemoteSetElementId next_id = 1;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_REMOTE_SET_H_
