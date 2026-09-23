// SharedRemote<Interface>: copyable handle to one bound Remote. Matching
// Chromium's mojo/public/cpp/bindings/shared_remote.h (in-process; this
// stack has no sequence checker).
#ifndef MOJO_PUBLIC_CPP_BINDINGS_SHARED_REMOTE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_SHARED_REMOTE_H_

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"

#include <memory>
#include <utility>

namespace mojo {

template <typename Interface>
class SharedRemote {
 public:
  SharedRemote() = default;
  explicit SharedRemote(PendingRemote<Interface> pending)
      : remote_(std::make_shared<Remote<Interface>>()) {
    remote_->Bind(std::move(pending));
  }
  SharedRemote(const SharedRemote&) = default;
  SharedRemote& operator=(const SharedRemote&) = default;
  SharedRemote(SharedRemote&&) noexcept = default;
  SharedRemote& operator=(SharedRemote&&) noexcept = default;

  bool is_bound() const { return remote_ && remote_->is_bound(); }
  explicit operator bool() const { return is_bound(); }

  Interface* get() const { return remote_ ? remote_->get() : nullptr; }
  Interface* operator->() const { return get(); }
  Interface& operator*() const { return *get(); }

  void reset() {
    if (remote_) {
      remote_->reset();
    }
  }

  void FlushForTesting() {
    if (remote_) {
      remote_->FlushForTesting();
    }
  }

 private:
  std::shared_ptr<Remote<Interface>> remote_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_SHARED_REMOTE_H_
