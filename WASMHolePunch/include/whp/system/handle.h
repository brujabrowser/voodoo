#ifndef WHP_SYSTEM_HANDLE_H_
#define WHP_SYSTEM_HANDLE_H_

#include "whp/c/system.h"

#include <utility>

namespace whp {

class ScopedHandle {
 public:
  ScopedHandle() = default;
  explicit ScopedHandle(WhpHandle h) : handle_(h) {}
  ~ScopedHandle() { reset(); }

  ScopedHandle(ScopedHandle&& o) noexcept : handle_(o.release()) {}
  ScopedHandle& operator=(ScopedHandle&& o) noexcept {
    if (this != &o) {
      reset();
      handle_ = o.release();
    }
    return *this;
  }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  bool is_valid() const { return handle_ != WHP_HANDLE_INVALID; }
  WhpHandle get() const { return handle_; }
  WhpHandle release() {
    WhpHandle h = handle_;
    handle_ = WHP_HANDLE_INVALID;
    return h;
  }
  void reset() {
    if (handle_ != WHP_HANDLE_INVALID) {
      WhpClose(handle_);
      handle_ = WHP_HANDLE_INVALID;
    }
  }

 private:
  WhpHandle handle_ = WHP_HANDLE_INVALID;
};

class ScopedMessageHandle {
 public:
  ScopedMessageHandle() = default;
  explicit ScopedMessageHandle(WhpMessageHandle h) : handle_(h) {}
  ~ScopedMessageHandle() { reset(); }

  ScopedMessageHandle(ScopedMessageHandle&& o) noexcept : handle_(o.release()) {}
  ScopedMessageHandle& operator=(ScopedMessageHandle&& o) noexcept {
    if (this != &o) {
      reset();
      handle_ = o.release();
    }
    return *this;
  }
  ScopedMessageHandle(const ScopedMessageHandle&) = delete;
  ScopedMessageHandle& operator=(const ScopedMessageHandle&) = delete;

  bool is_valid() const { return handle_ != WHP_MESSAGE_HANDLE_INVALID; }
  WhpMessageHandle get() const { return handle_; }
  WhpMessageHandle release() {
    WhpMessageHandle h = handle_;
    handle_ = WHP_MESSAGE_HANDLE_INVALID;
    return h;
  }
  void reset() {
    if (handle_ != WHP_MESSAGE_HANDLE_INVALID) {
      WhpDestroyMessage(handle_);
      handle_ = WHP_MESSAGE_HANDLE_INVALID;
    }
  }

 private:
  WhpMessageHandle handle_ = WHP_MESSAGE_HANDLE_INVALID;
};

using ScopedMessagePipeHandle = ScopedHandle;

}  // namespace whp

#endif  // WHP_SYSTEM_HANDLE_H_
