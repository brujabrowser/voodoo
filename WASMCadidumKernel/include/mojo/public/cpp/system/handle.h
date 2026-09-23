// Our own reimplementation of the public mojo/public/cpp/system/handle.h
// shape (not vendored Chromium source) so unmodified upstream
// mojo/public/cpp code compiles against this. Every MojoClose() below goes
// through WASMThunker's wst -> WASMHolePunch's whp::c.
#ifndef MOJO_PUBLIC_CPP_SYSTEM_HANDLE_H_
#define MOJO_PUBLIC_CPP_SYSTEM_HANDLE_H_

#include "mojo/public/c/system/core.h"

#include <algorithm>
#include <utility>

namespace mojo {

constexpr MojoHandle kInvalidHandleValue = MOJO_HANDLE_INVALID;

class Handle {
 public:
  Handle() : value_(kInvalidHandleValue) {}
  explicit Handle(MojoHandle value) : value_(value) {}
  ~Handle() = default;

  void swap(Handle& other) { std::swap(value_, other.value_); }
  bool is_valid() const { return value_ != kInvalidHandleValue; }
  MojoHandle value() const { return value_; }
  MojoHandle* mutable_value() { return &value_; }
  void set_value(MojoHandle value) { value_ = value; }

 protected:
  MojoHandle value_;
};

inline bool operator==(const Handle a, const Handle b) {
  return a.value() == b.value();
}
inline bool operator!=(const Handle a, const Handle b) {
  return !(a == b);
}

using HandleSignalsState = MojoHandleSignalsState;

// RAII base for the typed handle wrappers below. Mirrors Mojo's
// ScopedHandleBase<T>: move-only, closes via MojoClose() on destruction.
template <class HandleType>
class ScopedHandleBase {
 public:
  ScopedHandleBase() = default;
  explicit ScopedHandleBase(HandleType handle) : handle_(handle) {}
  ~ScopedHandleBase() { CloseIfNecessary(); }

  ScopedHandleBase(ScopedHandleBase&& other) noexcept
      : handle_(other.release()) {}
  ScopedHandleBase& operator=(ScopedHandleBase&& other) noexcept {
    if (this != &other) {
      CloseIfNecessary();
      handle_ = other.release();
    }
    return *this;
  }
  ScopedHandleBase(const ScopedHandleBase&) = delete;
  ScopedHandleBase& operator=(const ScopedHandleBase&) = delete;

  const HandleType& get() const { return handle_; }
  const HandleType* operator->() const { return &handle_; }

  bool is_valid() const { return handle_.is_valid(); }
  explicit operator bool() const { return handle_.is_valid(); }

  void swap(ScopedHandleBase& other) { handle_.swap(other.handle_); }

  HandleType release() {
    HandleType h;
    h.swap(handle_);
    return h;
  }

  void reset(HandleType handle = HandleType()) {
    CloseIfNecessary();
    handle_ = handle;
  }

 private:
  void CloseIfNecessary() {
    if (!handle_.is_valid()) {
      return;
    }
    MojoClose(handle_.value());
    handle_.set_value(kInvalidHandleValue);
  }

  HandleType handle_;
};

using ScopedHandle = ScopedHandleBase<Handle>;

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_HANDLE_H_
